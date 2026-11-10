package com.gamescrcpy.server.audio;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import com.gamescrcpy.protocol.Messages.AudioSessionInfo;
import java.io.IOException;
import java.nio.ByteBuffer;

final class AudioPackets {

    interface Sink {
        void write(byte[] header, ByteBuffer payload) throws IOException;
    }

    private final Sink sink;
    private final byte[] header = new byte[Constants.MEDIA_HEADER_SIZE];

    AudioPackets(Sink sink) {
        this.sink = sink;
    }

    void session(int session, AudioSessionInfo info) throws IOException {
        final byte[] packet = Media.encodeAudioSession(session, info);
        System.arraycopy(packet, 0, header, 0, header.length);
        sink.write(header, ByteBuffer.wrap(packet, header.length, packet.length - header.length));
    }

    void config(int session, ByteBuffer data) throws IOException {
        Media.writeHeader(header, 0, MediaChannel.AUDIO, MediaKind.CONFIG, 0, session, 0, 0, data.remaining());
        sink.write(header, data);
    }

    void frame(int session, long ptsUs, long encodeDelayUs, ByteBuffer data) throws IOException {
        Media.writeHeader(header, 0, MediaChannel.AUDIO, MediaKind.FRAME, 0, session, ptsUs, encodeDelayUs,
                data.remaining());
        sink.write(header, data);
    }
}
