package com.gamescrcpy.server.transport;

import java.io.Closeable;
import java.io.IOException;

public interface SessionTransport extends Closeable {

    interface Events {
        void sessionRequested(SessionRequest request);

        void channelRequested(ChannelRequest request);
    }

    void start(Events events);

    @Override
    void close() throws IOException;
}
