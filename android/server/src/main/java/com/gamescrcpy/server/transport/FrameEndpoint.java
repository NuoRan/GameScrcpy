package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import java.io.Closeable;
import java.io.IOException;

public interface FrameEndpoint extends Closeable {

    Message read(FrameContext context) throws IOException;

    void write(Message message) throws IOException;

    void setReadTimeout(int milliseconds) throws IOException;

    @Override
    void close();
}
