package com.gamescrcpy.server.transport;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;

public interface Connection extends Closeable {

    InputStream input() throws IOException;

    OutputStream output() throws IOException;

    void setReadTimeout(int milliseconds) throws IOException;

    default void setSendBufferSize(int bytes) throws IOException {
    }

    default void writePacket(byte[] header, ByteBuffer payload) throws IOException {
        final OutputStream out = output();
        out.write(header);
        if (payload.hasArray()) {
            out.write(payload.array(), payload.arrayOffset() + payload.position(), payload.remaining());
            payload.position(payload.limit());
        } else {
            final byte[] copy = new byte[payload.remaining()];
            payload.get(copy);
            out.write(copy);
        }
        out.flush();
    }
}
