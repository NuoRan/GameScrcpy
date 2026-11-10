package com.gamescrcpy.protocol;

import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Messages.AudioSessionInfo;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;

public final class Media {

    public static final int FLAG_KEY_FRAME = 0x01;
    public static final int FLAG_DISCARDABLE = 0x02;

    private Media() {
    }

    public static final class Header {
        public MediaKind kind = MediaKind.FRAME;
        public boolean keyFrame;
        public boolean discardable;
        public int session;
        public long ptsUs;
        public long encodeDelayUs;
        public int size;
    }

    public static final class Packet {
        public final Header header;
        public final Struct session;
        public final int payloadOffset;

        Packet(Header header, Struct session, int payloadOffset) {
            this.header = header;
            this.session = session;
            this.payloadOffset = payloadOffset;
        }
    }

    public static byte[] encodeVideoSession(int session, VideoSessionInfo info) {
        return pack(MediaChannel.VIDEO, MediaKind.SESSION, 0, session, 0, 0, encodeStruct(info));
    }

    public static byte[] encodeAudioSession(int session, AudioSessionInfo info) {
        return pack(MediaChannel.AUDIO, MediaKind.SESSION, 0, session, 0, 0, encodeStruct(info));
    }

    public static byte[] encodeConfig(MediaChannel channel, int session, byte[] payload) {
        return pack(channel, MediaKind.CONFIG, 0, session, 0, 0, payload);
    }

    public static byte[] encodeFrame(MediaChannel channel, int session, int flags, long ptsUs,
                                     long encodeDelayUs, byte[] payload) {
        return pack(channel, MediaKind.FRAME, flags, session, ptsUs, encodeDelayUs, payload);
    }

    public static void writeHeader(byte[] out, int offset, MediaChannel channel, MediaKind kind,
                                   int flags, int session, long ptsUs, long encodeDelayUs, int size) {
        final int allowedFlags = kind == MediaKind.FRAME ? FLAG_KEY_FRAME | FLAG_DISCARDABLE : 0;
        if (session < 1 || session > 0xFFFF || (flags & ~allowedFlags) != 0
                || encodeDelayUs < 0 || encodeDelayUs > 0xFFFF_FFFFL
                || (kind != MediaKind.FRAME && (ptsUs != 0 || encodeDelayUs != 0)) || size < 0) {
            throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
        }
        if (size > channel.payloadLimit()) {
            throw new EncodeException(EncodeError.FRAME_TOO_LARGE);
        }
        if (out.length - offset < Constants.MEDIA_HEADER_SIZE) {
            throw new EncodeException(EncodeError.BUFFER_TOO_SMALL);
        }
        ByteOrder.writeU8(out, offset, kind.wireValue());
        ByteOrder.writeU8(out, offset + 1, flags);
        ByteOrder.writeU16(out, offset + 2, session);
        ByteOrder.writeU64(out, offset + 4, ptsUs);
        ByteOrder.writeU32(out, offset + 12, encodeDelayUs);
        ByteOrder.writeU32(out, offset + 16, size);
    }

    private static byte[] pack(MediaChannel channel, MediaKind kind, int flags, int session,
                               long ptsUs, long encodeDelayUs, byte[] payload) {
        final byte[] packet = new byte[Constants.MEDIA_HEADER_SIZE + payload.length];
        writeHeader(packet, 0, channel, kind, flags, session, ptsUs, encodeDelayUs, payload.length);
        System.arraycopy(payload, 0, packet, Constants.MEDIA_HEADER_SIZE, payload.length);
        return packet;
    }

    private static byte[] encodeStruct(Struct struct) {
        final byte[] payload = new byte[EncodeVisitor.measure(struct)];
        EncodeVisitor.write(struct, payload, 0);
        return payload;
    }

    public static Header decodeHeader(MediaChannel channel, byte[] buffer, int offset, int length) {
        if (length < Constants.MEDIA_HEADER_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final MediaKind kind = MediaKind.CODEC.fromWire(ByteOrder.readU8(buffer, offset));
        if (kind == null) {
            throw new DecodeException(DecodeError.INVALID_ENUM);
        }
        final int flags = ByteOrder.readU8(buffer, offset + 1);
        final int allowedFlags = kind == MediaKind.FRAME ? FLAG_KEY_FRAME | FLAG_DISCARDABLE : 0;
        if ((flags & ~allowedFlags) != 0) {
            throw new DecodeException(DecodeError.RESERVED_BITS_SET);
        }
        final Header header = new Header();
        header.kind = kind;
        header.keyFrame = (flags & FLAG_KEY_FRAME) != 0;
        header.discardable = (flags & FLAG_DISCARDABLE) != 0;
        header.session = ByteOrder.readU16(buffer, offset + 2);
        header.ptsUs = ByteOrder.readU64(buffer, offset + 4);
        header.encodeDelayUs = ByteOrder.readU32(buffer, offset + 12);
        final long size = ByteOrder.readU32(buffer, offset + 16);
        if (header.session == 0) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        if (kind != MediaKind.FRAME && (header.ptsUs != 0 || header.encodeDelayUs != 0)) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        if (size > channel.payloadLimit()) {
            throw new DecodeException(DecodeError.FRAME_TOO_LARGE);
        }
        header.size = (int) size;
        return header;
    }

    public static Packet decodePacket(MediaChannel channel, byte[] buffer, int offset, int length) {
        final Header header = decodeHeader(channel, buffer, offset, length);
        final int payloadOffset = offset + Constants.MEDIA_HEADER_SIZE;
        if (length - Constants.MEDIA_HEADER_SIZE < header.size) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        Struct session = null;
        if (header.kind == MediaKind.SESSION) {
            session = channel == MediaChannel.VIDEO ? new VideoSessionInfo() : new AudioSessionInfo();
            Codec.decodeStruct(session, buffer, payloadOffset, header.size);
        }
        return new Packet(header, session, payloadOffset);
    }
}
