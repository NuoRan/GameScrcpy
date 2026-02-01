package com.genymobile.scrcpy.device;

import com.genymobile.scrcpy.control.ControlChannel;
import com.genymobile.scrcpy.util.IO;
import com.genymobile.scrcpy.util.StringUtils;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

public final class DesktopConnection implements Closeable {

    private static final int DEVICE_NAME_FIELD_LENGTH = 64;

    private static final String SOCKET_NAME_PREFIX = "scrcpy";

    private final LocalSocket videoSocket;
    private final FileDescriptor videoFd;

    private final LocalSocket controlSocket;
    private final ControlChannel controlChannel;

    private DesktopConnection(LocalSocket videoSocket, LocalSocket controlSocket) throws IOException {
        this.videoSocket = videoSocket;
        this.controlSocket = controlSocket;

        videoFd = videoSocket != null ? videoSocket.getFileDescriptor() : null;
        controlChannel = controlSocket != null ? new ControlChannel(controlSocket) : null;
    }

    private static LocalSocket connect(String abstractName) throws IOException {
        LocalSocket localSocket = new LocalSocket();
        localSocket.connect(new LocalSocketAddress(abstractName));
        return localSocket;
    }

    private static String getSocketName(int scid) {
        if (scid == -1) {
            return SOCKET_NAME_PREFIX;
        }
        return SOCKET_NAME_PREFIX + String.format("_%08x", scid);
    }

    public static DesktopConnection open(int scid, boolean tunnelForward, boolean video, boolean audio, boolean control, boolean sendDummyByte)
            throws IOException {
        String baseSocketName = getSocketName(scid);
        String videoSocketName = baseSocketName + "_video";
        String controlSocketName = baseSocketName + "_control";

        LocalSocket videoSocket = null;
        LocalSocket controlSocket = null;

        try {
            if (tunnelForward) {
                if (video) {
                    try (LocalServerSocket videoServerSocket = new LocalServerSocket(videoSocketName)) {
                        videoSocket = videoServerSocket.accept();
                        if (sendDummyByte) {
                            videoSocket.getOutputStream().write(0);
                        }
                    }
                }

                if (control) {
                    try (LocalServerSocket controlServerSocket = new LocalServerSocket(controlSocketName)) {
                        controlSocket = controlServerSocket.accept();
                        if (sendDummyByte) {
                            controlSocket.getOutputStream().write(0);
                        }
                    }
                }
            } else {
                if (video) {
                    videoSocket = connect(videoSocketName);
                }
                if (control) {
                    controlSocket = connect(controlSocketName);
                }
            }
        } catch (IOException | RuntimeException e) {
            if (videoSocket != null) {
                videoSocket.close();
            }
            if (controlSocket != null) {
                controlSocket.close();
            }
            throw e;
        }

        return new DesktopConnection(videoSocket, controlSocket);
    }

    private LocalSocket getFirstSocket() {
        if (videoSocket != null) {
            return videoSocket;
        }
        return controlSocket;
    }

    public void shutdown() throws IOException {
        if (videoSocket != null) {
            videoSocket.shutdownInput();
            videoSocket.shutdownOutput();
        }
        if (controlSocket != null) {
            controlSocket.shutdownInput();
            controlSocket.shutdownOutput();
        }
    }

    public void close() throws IOException {
        if (videoSocket != null) {
            videoSocket.close();
        }
        if (controlSocket != null) {
            controlSocket.close();
        }
    }

    public void sendDeviceMeta(String deviceName) throws IOException {
        byte[] buffer = new byte[DEVICE_NAME_FIELD_LENGTH];

        byte[] deviceNameBytes = deviceName.getBytes(StandardCharsets.UTF_8);
        int len = StringUtils.getUtf8TruncationIndex(deviceNameBytes, DEVICE_NAME_FIELD_LENGTH - 1);
        System.arraycopy(deviceNameBytes, 0, buffer, 0, len);

        FileDescriptor fd = getFirstSocket().getFileDescriptor();
        IO.writeFully(fd, buffer, 0, buffer.length);
    }

    public FileDescriptor getVideoFd() {
        return videoFd;
    }

    public ControlChannel getControlChannel() {
        return controlChannel;
    }
}
