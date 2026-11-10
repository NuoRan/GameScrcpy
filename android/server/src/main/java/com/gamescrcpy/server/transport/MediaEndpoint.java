package com.gamescrcpy.server.transport;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;

public interface MediaEndpoint extends Closeable {

    void writePacket(byte[] header, ByteBuffer payload) throws IOException;

    default void setSendBufferSize(int bytes) throws IOException {
    }

    default void feedback(long lostShards, long rebuiltShards) {
    }

    @Override
    void close();
}
