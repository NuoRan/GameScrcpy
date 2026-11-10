package com.gamescrcpy.server.uhid;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructPollfd;
import java.io.FileDescriptor;
import java.io.IOException;

public final class DevUhid implements UhidFile {

    private static final String PATH = "/dev/uhid";

    private final FileDescriptor device;
    private final FileDescriptor wakeRead;
    private final FileDescriptor wakeWrite;
    private final Object lock = new Object();
    private boolean stopping;
    private boolean reading;

    private DevUhid(FileDescriptor device, FileDescriptor wakeRead, FileDescriptor wakeWrite) {
        this.device = device;
        this.wakeRead = wakeRead;
        this.wakeWrite = wakeWrite;
    }

    public static UhidFile open() throws IOException {
        final FileDescriptor device;
        try {
            device = Os.open(PATH, OsConstants.O_RDWR | OsConstants.O_CLOEXEC, 0);
        } catch (ErrnoException e) {
            throw new IOException("cannot open " + PATH + ": " + e.getMessage(), e);
        }
        try {
            final FileDescriptor[] wake = Os.pipe();
            return new DevUhid(device, wake[0], wake[1]);
        } catch (ErrnoException e) {
            closeQuietly(device);
            throw new IOException("cannot make a wake-up pipe for " + PATH + ": " + e.getMessage(), e);
        }
    }

    @Override
    public int read(byte[] buffer) throws IOException {
        synchronized (lock) {
            if (stopping) {
                return -1;
            }
            reading = true;
        }
        try {
            return waitAndRead(buffer);
        } finally {
            synchronized (lock) {
                reading = false;
                if (stopping) {
                    closeQuietly(device);
                    closeQuietly(wakeRead);
                }
            }
        }
    }

    @Override
    public void write(byte[] event) throws IOException {
        synchronized (lock) {
            if (stopping) {
                throw new IOException(PATH + " is closed");
            }
            while (true) {
                try {
                    final int written = Os.write(device, event, 0, event.length);
                    if (written != event.length) {
                        throw new IOException(PATH + " took " + written + " of " + event.length + " bytes");
                    }
                    return;
                } catch (ErrnoException e) {
                    if (e.errno != OsConstants.EINTR) {
                        throw new IOException("cannot write to " + PATH + ": " + e.getMessage(), e);
                    }
                }
            }
        }
    }

    @Override
    public void close() {
        synchronized (lock) {
            if (stopping) {
                return;
            }
            stopping = true;
            closeQuietly(wakeWrite);
            if (!reading) {
                closeQuietly(device);
                closeQuietly(wakeRead);
            }
        }
    }

    private int waitAndRead(byte[] buffer) throws IOException {
        final StructPollfd events = new StructPollfd();
        events.fd = device;
        events.events = (short) OsConstants.POLLIN;
        final StructPollfd wake = new StructPollfd();
        wake.fd = wakeRead;
        wake.events = (short) OsConstants.POLLIN;
        final StructPollfd[] watched = {events, wake};
        while (true) {
            events.revents = 0;
            wake.revents = 0;
            try {
                Os.poll(watched, -1);
            } catch (ErrnoException e) {
                if (e.errno == OsConstants.EINTR) {
                    continue;
                }
                throw new IOException("cannot wait for " + PATH + ": " + e.getMessage(), e);
            }
            if (wake.revents != 0) {
                return -1;
            }
            if ((events.revents & OsConstants.POLLIN) != 0) {
                try {
                    final int size = Os.read(device, buffer, 0, buffer.length);
                    return size > 0 ? size : -1;
                } catch (ErrnoException e) {
                    if (e.errno == OsConstants.EINTR || e.errno == OsConstants.EAGAIN) {
                        continue;
                    }
                    throw new IOException("cannot read " + PATH + ": " + e.getMessage(), e);
                }
            }
            if (events.revents != 0) {
                return -1;
            }
        }
    }

    private static void closeQuietly(FileDescriptor fd) {
        try {
            Os.close(fd);
        } catch (ErrnoException ignored) {
        }
    }
}
