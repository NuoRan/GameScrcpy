package com.gamescrcpy.server.uhid;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

final class FakeUhidFile implements UhidFile {

    private static final byte[] CLOSED = new byte[0];

    private final BlockingQueue<byte[]> kernel = new LinkedBlockingQueue<>();
    private final BlockingQueue<byte[]> written = new LinkedBlockingQueue<>();
    private volatile boolean closed;
    volatile boolean refuseWrites;

    @Override
    public int read(byte[] buffer) throws IOException {
        final byte[] event;
        try {
            event = kernel.take();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new InterruptedIOException("interrupted while waiting for the kernel");
        }
        if (event == CLOSED) {
            return -1;
        }
        final int size = Math.min(event.length, buffer.length);
        System.arraycopy(event, 0, buffer, 0, size);
        return size;
    }

    @Override
    public void write(byte[] event) throws IOException {
        if (closed || refuseWrites) {
            throw new IOException("the fake device refuses writes");
        }
        written.add(event.clone());
    }

    @Override
    public void close() {
        closed = true;
        kernel.add(CLOSED);
    }

    boolean closed() {
        return closed;
    }

    void event(byte[] event) {
        kernel.add(event);
    }

    byte[] nextWrite() throws InterruptedException {
        return written.poll(5, TimeUnit.SECONDS);
    }

    byte[] writtenSoFar() {
        return written.poll();
    }
}
