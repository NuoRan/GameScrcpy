package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.Welcome;
import java.io.IOException;

public interface SessionRequest {

    ClientHello hello();

    boolean proven();

    FrameEndpoint accept(Welcome welcome) throws IOException;

    default FrameEndpoint acceptedBulk() {
        return null;
    }

    void reject(ErrorCode code, String message);
}
