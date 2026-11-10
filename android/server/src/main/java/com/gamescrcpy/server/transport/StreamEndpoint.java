package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.DecodeError;
import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.MessageType;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.EnumMap;
import java.util.Map;

public final class StreamEndpoint implements FrameEndpoint, MediaEndpoint {

    private final Connection connection;
    private final InputStream input;
    private final OutputStream output;
    private final byte[] header = new byte[Constants.FRAME_HEADER_SIZE];
    private final Map<MessageType, Message> resident = new EnumMap<>(MessageType.class);
    private byte[] payload = new byte[256];

    public StreamEndpoint(Connection connection) throws IOException {
        this.connection = connection;
        this.input = connection.input();
        this.output = connection.output();
    }

    @Override
    public void setReadTimeout(int milliseconds) throws IOException {
        connection.setReadTimeout(milliseconds);
    }

    @Override
    public void setSendBufferSize(int bytes) throws IOException {
        connection.setSendBufferSize(bytes);
    }

    @Override
    public Message read(FrameContext context) throws IOException {
        readFully(header, Constants.FRAME_HEADER_SIZE);
        final Codec.FrameHeader frame = Codec.decodeFrameHeader(context, Direction.TO_SERVER, header, 0,
                Constants.FRAME_HEADER_SIZE);
        if (payload.length < frame.length) {
            payload = new byte[frame.length];
        }
        readFully(payload, frame.length);
        final Message message = context == FrameContext.CONTROL
                ? resident.computeIfAbsent(frame.type, MessageType::create) : frame.type.create();
        return Codec.decodePayload(message, payload, 0, frame.length);
    }

    @Override
    public synchronized void write(Message message) throws IOException {
        output.write(Codec.encodeFrame(message));
        output.flush();
    }

    @Override
    public synchronized void writePacket(byte[] header, ByteBuffer payload) throws IOException {
        connection.writePacket(header, payload);
    }

    @Override
    public void close() {
        try {
            connection.close();
        } catch (IOException ignored) {
        }
    }

    private void readFully(byte[] buffer, int length) throws IOException {
        int offset = 0;
        while (offset < length) {
            final int read = input.read(buffer, offset, length - offset);
            if (read < 0) {
                if (offset == 0 && buffer == header) {
                    throw new EOFException();
                }
                throw new DecodeException(DecodeError.TRUNCATED);
            }
            offset += read;
        }
    }
}
