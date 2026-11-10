package com.gamescrcpy.protocol;

public final class Codec {

    private Codec() {
    }

    public static final class FrameHeader {
        public final MessageType type;
        public final int length;

        FrameHeader(MessageType type, int length) {
            this.type = type;
            this.length = length;
        }
    }

    public static int encodedPayloadSize(Message message) {
        final int size = EncodeVisitor.measure(message);
        if (!message.isValid()) {
            throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
        }
        return size;
    }

    public static int encodeFrameInto(Message message, byte[] out, int offset) {
        final int payloadSize = encodedPayloadSize(message);
        if (payloadSize > message.type().context().payloadLimit()) {
            throw new EncodeException(EncodeError.FRAME_TOO_LARGE);
        }
        final int frameSize = Constants.FRAME_HEADER_SIZE + payloadSize;
        if (out.length - offset < frameSize) {
            throw new EncodeException(EncodeError.BUFFER_TOO_SMALL);
        }
        ByteOrder.writeU16(out, offset, message.type().code());
        ByteOrder.writeU32(out, offset + 2, payloadSize);
        EncodeVisitor.write(message, out, offset + Constants.FRAME_HEADER_SIZE);
        return frameSize;
    }

    public static byte[] encodeFrame(Message message) {
        final byte[] frame = new byte[Constants.FRAME_HEADER_SIZE + encodedPayloadSize(message)];
        encodeFrameInto(message, frame, 0);
        return frame;
    }

    public static FrameHeader decodeFrameHeader(FrameContext context, Direction direction,
                                                byte[] buffer, int offset, int length) {
        if (length < Constants.FRAME_HEADER_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final MessageType type = MessageType.fromCode(ByteOrder.readU16(buffer, offset));
        if (type == null) {
            throw new DecodeException(DecodeError.UNKNOWN_TYPE);
        }
        if (type.direction() != direction) {
            throw new DecodeException(DecodeError.WRONG_DIRECTION);
        }
        if (!context.allows(type)) {
            throw new DecodeException(DecodeError.UNEXPECTED_TYPE);
        }
        final long payloadLength = ByteOrder.readU32(buffer, offset + 2);
        if (payloadLength > context.payloadLimit()) {
            throw new DecodeException(DecodeError.FRAME_TOO_LARGE);
        }
        return new FrameHeader(type, (int) payloadLength);
    }

    public static Message decodeFrame(FrameContext context, Direction direction, byte[] buffer,
                                      int offset, int length) {
        final FrameHeader header = decodeFrameHeader(context, direction, buffer, offset, length);
        if (length - Constants.FRAME_HEADER_SIZE < header.length) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        return decodePayload(header.type.create(), buffer, offset + Constants.FRAME_HEADER_SIZE,
                header.length);
    }

    public static <M extends Message> M decodePayload(M message, byte[] buffer, int offset, int length) {
        decodeStruct(message, buffer, offset, length);
        if (!message.isValid()) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        return message;
    }

    static void decodeStruct(Struct struct, byte[] buffer, int offset, int length) {
        final DecodeVisitor reader = new DecodeVisitor(buffer, offset, length);
        struct.visit(reader);
        if (reader.remaining() != 0) {
            throw new DecodeException(DecodeError.TRAILING_BYTES);
        }
    }
}
