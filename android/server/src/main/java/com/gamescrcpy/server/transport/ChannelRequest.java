package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import java.io.IOException;

public interface ChannelRequest {

    ChannelId channel();

    MediaEndpoint acceptMedia() throws IOException;

    FrameEndpoint acceptFrames() throws IOException;

    void reject(ErrorCode code, String message);
}
