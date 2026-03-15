package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.util.StringUtils;
import com.genymobile.scrcpy.wrappers.ServiceManager;

import android.os.Build;
import android.os.HandlerThread;
import android.os.MessageQueue;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.util.ArrayMap;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

public final class UhidManager {

    // Linux: include/uapi/linux/uhid.h
    private static final int UHID_OUTPUT = 6;
    private static final int UHID_CREATE2 = 11;
    private static final int UHID_INPUT2 = 12;

    // Linux: include/uapi/linux/input.h
    private static final short BUS_VIRTUAL = 0x06;

    private static final int SIZE_OF_UHID_EVENT = 4380;

    private static final String INPUT_PORT = "gamescrcpy:" + Os.getpid();

    private final ArrayMap<Integer, FileDescriptor> fds = new ArrayMap<>();
    private final ByteBuffer buffer = ByteBuffer.allocate(SIZE_OF_UHID_EVENT).order(ByteOrder.nativeOrder());

    private final DeviceMessageSender sender;
    private final MessageQueue queue;

    public UhidManager(DeviceMessageSender sender) {
        this.sender = sender;
        if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0) {
            HandlerThread thread = new HandlerThread("UHidManager");
            thread.start();
            queue = thread.getLooper().getQueue();
        } else {
            queue = null;
        }
    }

    public void open(int id, int vendorId, int productId, String name, byte[] reportDesc) throws IOException {
        try {
            FileDescriptor fd = Os.open("/dev/uhid", OsConstants.O_RDWR, 0);
            try {
                FileDescriptor old = fds.put(id, fd);
                if (old != null) {
                    Ln.w("Duplicate UHID id: " + id);
                    close(old);
                }

                byte[] req = buildUhidCreate2Req(vendorId, productId, name, reportDesc);
                Os.write(fd, req, 0, req.length);
                Ln.i("UHID device created: id=" + id + " descSize=" + reportDesc.length);

                registerUhidListener(id, fd);
            } catch (Exception e) {
                close(fd);
                throw e;
            }
        } catch (ErrnoException e) {
            throw new IOException(e);
        }
    }

    private void registerUhidListener(int id, FileDescriptor fd) {
        if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0) {
            queue.addOnFileDescriptorEventListener(fd, MessageQueue.OnFileDescriptorEventListener.EVENT_INPUT, (fd2, events) -> {
                try {
                    buffer.clear();
                    int r = Os.read(fd2, buffer);
                    buffer.flip();
                    if (r > 0) {
                        int type = buffer.getInt();
                        if (type == UHID_OUTPUT) {
                            byte[] data = extractHidOutputData(buffer);
                            if (data != null) {
                                DeviceMessage msg = DeviceMessage.createUhidOutput(id, data);
                                sender.send(msg);
                            }
                        }
                    }
                } catch (ErrnoException | InterruptedIOException e) {
                    Ln.e("Failed to read UHID output", e);
                    return 0;
                }
                return events;
            });
        }
    }

    private void unregisterUhidListener(FileDescriptor fd) {
        if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0) {
            queue.removeOnFileDescriptorEventListener(fd);
        }
    }

    private static byte[] extractHidOutputData(ByteBuffer buffer) {
        if (buffer.remaining() < 4099) {
            Ln.w("Incomplete HID output");
            return null;
        }
        int size = buffer.getShort(buffer.position() + 4096) & 0xFFFF;
        if (size > 4096) {
            Ln.w("Incorrect HID output size: " + size);
            return null;
        }
        byte[] data = new byte[size];
        buffer.get(data);
        return data;
    }

    public void writeInput(int id, byte[] data) throws IOException {
        FileDescriptor fd = fds.get(id);
        if (fd == null) {
            Ln.w("Unknown UHID id: " + id);
            return;
        }

        try {
            byte[] req = buildUhidInput2Req(data);
            Os.write(fd, req, 0, req.length);
        } catch (ErrnoException e) {
            throw new IOException(e);
        }
    }

    private static byte[] buildUhidCreate2Req(int vendorId, int productId, String name, byte[] reportDesc) {
        ByteBuffer buf = ByteBuffer.allocate(280 + reportDesc.length).order(ByteOrder.nativeOrder());
        buf.putInt(UHID_CREATE2);

        String actualName = (name == null || name.isEmpty()) ? "gamescrcpy" : name;
        byte[] nameBytes = actualName.getBytes(StandardCharsets.UTF_8);
        int nameLen = StringUtils.getUtf8TruncationIndex(nameBytes, 127);
        buf.put(nameBytes, 0, nameLen);

        // phys at offset 4+128
        buf.position(4 + 128);
        byte[] physBytes = INPUT_PORT.getBytes(StandardCharsets.US_ASCII);
        if (physBytes.length <= 63) {
            buf.put(physBytes);
        }

        // rd_size, bus, vendor, product, version, country at offset 4+256
        buf.position(4 + 256);
        buf.putShort((short) reportDesc.length);
        buf.putShort(BUS_VIRTUAL);
        buf.putInt(vendorId);
        buf.putInt(productId);
        buf.putInt(0); // version
        buf.putInt(0); // country
        buf.put(reportDesc);
        return buf.array();
    }

    private static byte[] buildUhidInput2Req(byte[] data) {
        ByteBuffer buf = ByteBuffer.allocate(6 + data.length).order(ByteOrder.nativeOrder());
        buf.putInt(UHID_INPUT2);
        buf.putShort((short) data.length);
        buf.put(data);
        return buf.array();
    }

    public void close(int id) {
        FileDescriptor fd = fds.remove(id);
        if (fd != null) {
            unregisterUhidListener(fd);
            close(fd);
            Ln.i("UHID device closed: id=" + id);
        } else {
            Ln.w("Closing unknown UHID device: " + id);
        }
    }

    public void closeAll() {
        if (fds.isEmpty()) {
            return;
        }
        for (FileDescriptor fd : fds.values()) {
            close(fd);
        }
        fds.clear();
    }

    private static void close(FileDescriptor fd) {
        try {
            Os.close(fd);
        } catch (ErrnoException e) {
            Ln.e("Failed to close uhid: " + e.getMessage());
        }
    }
}
