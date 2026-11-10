package com.gamescrcpy.server.transport;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;

public final class LocalSocketListener implements Listener {

    private final LocalServerSocket server;

    public LocalSocketListener(String name) throws IOException {
        this.server = new LocalServerSocket(name);
    }

    @Override
    public Connection accept() throws IOException {
        return new LocalConnection(server.accept());
    }

    @Override
    public void close() throws IOException {
        server.close();
    }

    private static final class LocalConnection implements Connection {
        private final LocalSocket socket;
        private final Object[] vector = new Object[2];
        private final int[] offsets = new int[2];
        private final int[] counts = new int[2];

        LocalConnection(LocalSocket socket) {
            this.socket = socket;
        }

        @Override
        public InputStream input() throws IOException {
            return socket.getInputStream();
        }

        @Override
        public OutputStream output() throws IOException {
            return socket.getOutputStream();
        }

        @Override
        public void setReadTimeout(int milliseconds) throws IOException {
            socket.setSoTimeout(milliseconds);
        }

        @Override
        public void setSendBufferSize(int bytes) throws IOException {
            socket.setSendBufferSize(bytes);
        }

        @Override
        public synchronized void writePacket(byte[] header, ByteBuffer payload) throws IOException {
            final FileDescriptor fd = socket.getFileDescriptor();
            if (!payload.isDirect() && !payload.hasArray()) {
                writeFully(fd, ByteBuffer.wrap(header));
                writeFully(fd, payload);
                return;
            }
            vector[0] = header;
            offsets[0] = 0;
            counts[0] = header.length;
            vector[1] = payload.isDirect() ? payload : payload.array();
            offsets[1] = payload.isDirect() ? payload.position() : payload.arrayOffset() + payload.position();
            counts[1] = payload.remaining();
            int written;
            while (true) {
                try {
                    written = Os.writev(fd, vector, offsets, counts);
                    break;
                } catch (ErrnoException e) {
                    if (e.errno != OsConstants.EINTR) {
                        throw new IOException("socket write failed", e);
                    }
                }
            }
            vector[1] = null;
            final int fromHeader = Math.min(written, header.length);
            if (fromHeader < header.length) {
                writeFully(fd, ByteBuffer.wrap(header, fromHeader, header.length - fromHeader));
            }
            payload.position(payload.position() + written - fromHeader);
            writeFully(fd, payload);
        }

        @Override
        public void close() throws IOException {
            try {
                socket.shutdownInput();
            } catch (IOException ignored) {
            }
            try {
                socket.shutdownOutput();
            } catch (IOException ignored) {
            }
            socket.close();
        }

        private static void writeFully(FileDescriptor fd, ByteBuffer buffer) throws IOException {
            while (buffer.hasRemaining()) {
                try {
                    Os.write(fd, buffer);
                } catch (ErrnoException e) {
                    if (e.errno != OsConstants.EINTR) {
                        throw new IOException("socket write failed", e);
                    }
                }
            }
        }
    }
}
