package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import java.nio.ByteBuffer;

final class NalUnits {

    private NalUnits() {
    }

    static boolean discardable(VideoCodec codec, ByteBuffer frame) {
        if (codec == VideoCodec.AV1) {
            return false;
        }
        int from = frame.position();
        final int limit = frame.limit();
        while (true) {
            final int header = nextUnit(frame, from, limit);
            if (header < 0) {
                return false;
            }
            final int value = frame.get(header) & 0xFF;
            if (codec == VideoCodec.H264) {
                final int type = value & 0x1F;
                if (type >= 1 && type <= 5) {
                    return (value >> 5) == 0;
                }
            } else {
                final int type = (value >> 1) & 0x3F;
                if (type <= 31) {
                    return type <= 14 && (type & 1) == 0;
                }
            }
            from = header + 1;
        }
    }

    private static int nextUnit(ByteBuffer buffer, int from, int limit) {
        for (int i = from; i + 3 < limit; ++i) {
            if (buffer.get(i + 2) == 1 && buffer.get(i + 1) == 0 && buffer.get(i) == 0) {
                return i + 3;
            }
        }
        return -1;
    }
}
