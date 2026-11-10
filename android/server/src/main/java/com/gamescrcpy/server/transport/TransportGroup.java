package com.gamescrcpy.server.transport;

import java.io.IOException;
import java.util.List;

public final class TransportGroup implements SessionTransport {

    private final List<SessionTransport> transports;

    public TransportGroup(SessionTransport... transports) {
        this.transports = List.of(transports);
    }

    @Override
    public void start(Events events) {
        for (SessionTransport transport : transports) {
            transport.start(events);
        }
    }

    @Override
    public void close() throws IOException {
        IOException failure = null;
        for (SessionTransport transport : transports) {
            try {
                transport.close();
            } catch (IOException e) {
                if (failure == null) {
                    failure = e;
                } else {
                    failure.addSuppressed(e);
                }
            }
        }
        if (failure != null) {
            throw failure;
        }
    }
}
