package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Datagram;
import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.DirectMedia;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.MessageType;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.protocol.ReedSolomon;
import com.gamescrcpy.protocol.Reliable;
import java.io.EOFException;
import java.io.IOException;
import java.net.DatagramPacket;
import java.nio.ByteBuffer;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.security.SecureRandom;
import java.util.EnumMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

public final class UdpTransport implements SessionTransport {

    static final int CONTROL_RECEIVE_LIMIT = 1 << 20;
    static final int BULK_RECEIVE_LIMIT = 8 << 20;
    static final long CLOSING_US = 1_000_000;
    static final long CLOSED_RESEND_US = 250_000;
    static final long CLOSED_GIVE_UP_US = 1_500_000;

    private final DatagramSocket socket;
    private final byte[] token;
    private final LongSupplier clock;
    private final UdpHandshake handshake;
    private final byte[] closedDatagram = new byte[DirectMedia.CLOSED_SIZE];
    private final Object lock = new Object();
    private Association association;
    private boolean closed;
    private Events events;
    private Thread receiver;
    private Thread sender;

    public UdpTransport(DatagramSocket socket, byte[] token, SecureRandom random, LongSupplier clockMicros) {
        this.socket = socket;
        this.token = token.clone();
        this.clock = clockMicros;
        this.handshake = new UdpHandshake(token, random, new UdpHandshake.Sink() {
            @Override
            public void send(byte[] datagram, int length, InetSocketAddress to) {
                UdpTransport.this.send(datagram, length, to);
            }

            @Override
            public void requested(UdpHandshake.Request request) {
                events.sessionRequested(new UdpSessionRequest(request));
            }
        });
    }

    public int port() {
        return socket.getLocalPort();
    }

    @Override
    public void start(Events events) {
        if (receiver != null) {
            throw new IllegalStateException("started twice");
        }
        this.events = events;
        receiver = new Thread(this::receiveLoop, "gs-udp-in");
        sender = new Thread(this::sendLoop, "gs-udp-out");
        receiver.setDaemon(true);
        sender.setDaemon(true);
        receiver.start();
        sender.start();
    }

    @Override
    public void close() {
        synchronized (lock) {
            if (!closed) {
                closed = true;
                if (association != null) {
                    association.close(clock.getAsLong());
                }
                lock.notifyAll();
                final long until = System.nanoTime() + TimeUnit.MICROSECONDS.toNanos(CLOSING_US);
                while (association != null && sender != null && sender.isAlive()) {
                    final long remaining = until - System.nanoTime();
                    if (remaining <= 0 || !waitNanos(remaining)) {
                        break;
                    }
                }
                association = null;
                lock.notifyAll();
            }
        }
        socket.close();
    }

    private void receiveLoop() {
        final byte[] buffer = new byte[Datagram.MAX_SIZE + 1];
        final DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
        while (true) {
            packet.setLength(buffer.length);
            try {
                socket.receive(packet);
            } catch (IOException e) {
                return;
            }
            final int length = packet.getLength();
            final InetSocketAddress from = (InetSocketAddress) packet.getSocketAddress();
            final Datagram.Header header;
            try {
                header = Datagram.decodeHeader(buffer, 0, length);
            } catch (DecodeException e) {
                continue;
            }
            final long now = clock.getAsLong();
            if (header.channel == Datagram.HANDSHAKE_CHANNEL) {
                handshake.receive(buffer, length, from, now);
            } else if (handshake.admits(from, header.connection)) {
                deliver(header, buffer, length, now);
            }
        }
    }

    private void deliver(Datagram.Header header, byte[] data, int length, long now) {
        ChannelRequest request = null;
        synchronized (lock) {
            final Association current = association;
            if (current == null || current.connection != header.connection) {
                return;
            }
            if (header.channel == DirectMedia.VIDEO_CHANNEL || header.channel == DirectMedia.AUDIO_CHANNEL) {
                if (length == Datagram.HEADER_SIZE) {
                    request = current.open(header.channel);
                } else if (DirectMedia.isClosed(data, length)) {
                    current.closeAcknowledged(header.channel);
                }
            } else {
                final ReliableStream stream = header.channel == Reliable.CONTROL_CHANNEL ? current.control : current.bulk;
                if (stream.receive(data, length, now) == ReliableStream.Verdict.BROKEN && current.broken == null) {
                    current.broken = stream.problem();
                }
                lock.notifyAll();
            }
        }
        if (request != null) {
            events.channelRequested(request);
        }
    }

    private void sendOpened(Association owner, int channel) {
        final byte[] opened = new byte[Datagram.HEADER_SIZE];
        Datagram.writeHeader(opened, 0, channel, owner.connection);
        send(opened, opened.length, owner.peer);
    }

    private void sendLoop() {
        synchronized (lock) {
            while (!(closed && association == null)) {
                final long now = clock.getAsLong();
                long wake = ReliableStream.NEVER;
                final Association current = association;
                if (current != null) {
                    flush(current.control, current.peer, now);
                    flush(current.bulk, current.peer, now);
                    if (current.closing && (current.delivered() || now >= current.closingDeadlineUs)) {
                        association = null;
                        lock.notifyAll();
                        continue;
                    }
                    wake = Math.min(current.control.deadline(), current.bulk.deadline());
                    if (current.closing) {
                        wake = Math.min(wake, current.closingDeadlineUs);
                    }
                    wake = Math.min(wake, sendClosed(current, now));
                }
                if (wake == ReliableStream.NEVER) {
                    if (!waitNanos(0)) {
                        return;
                    }
                } else if (wake > now && !waitNanos(TimeUnit.MICROSECONDS.toNanos(wake - now))) {
                    return;
                }
            }
        }
    }

    private long sendClosed(Association owner, long now) {
        long next = ReliableStream.NEVER;
        if (owner.closing) {
            return next;
        }
        for (int channel = DirectMedia.VIDEO_CHANNEL; channel <= DirectMedia.AUDIO_CHANNEL; ++channel) {
            final MediaSlot slot = owner.media[channel];
            if (slot == null || !slot.closing) {
                continue;
            }
            if (now >= slot.closingDeadlineUs) {
                owner.media[channel] = null;
                continue;
            }
            if (now >= slot.nextClosedUs) {
                send(closedDatagram, DirectMedia.writeClosed(closedDatagram, channel, owner.connection), owner.peer);
                slot.nextClosedUs = now + CLOSED_RESEND_US;
            }
            next = Math.min(next, Math.min(slot.nextClosedUs, slot.closingDeadlineUs));
        }
        return next;
    }

    private void flush(ReliableStream stream, InetSocketAddress peer, long now) {
        for (int length = stream.poll(now); length != 0; length = stream.poll(now)) {
            send(stream.datagram(), length, peer);
        }
    }

    private void send(byte[] datagram, int length, InetSocketAddress to) {
        try {
            socket.send(new DatagramPacket(datagram, length, to));
        } catch (IOException e) {
        }
    }

    private boolean waitNanos(long nanos) {
        try {
            if (nanos == 0) {
                lock.wait();
            } else {
                lock.wait(nanos / 1_000_000, (int) (nanos % 1_000_000));
            }
            return true;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    private final class Association {
        final long connection;
        final InetSocketAddress peer;
        final ReliableStream control;
        final ReliableStream bulk;
        final ControlEndpoint endpoint = new ControlEndpoint(this);
        final BulkEndpoint bulkEndpoint = new BulkEndpoint(this);
        final MediaSlot[] media = new MediaSlot[3];
        boolean closing;
        long closingDeadlineUs;
        String broken;

        ChannelRequest open(int channel) {
            final MediaSlot opened = media[channel];
            if (opened == null || opened.closing) {
                final MediaSlot slot = new MediaSlot(this, channel);
                media[channel] = slot;
                return slot.new Request();
            }
            if (opened.endpoint != null) {
                sendOpened(this, channel);
            }
            return null;
        }

        void closeAcknowledged(int channel) {
            final MediaSlot slot = media[channel];
            if (slot != null && slot.closing) {
                media[channel] = null;
            }
        }

        Association(long connection, InetSocketAddress peer) {
            this.connection = connection;
            this.peer = peer;
            this.control = new ReliableStream(ReliableStream.Kind.CONTROL, connection, token, CONTROL_RECEIVE_LIMIT);
            this.bulk = new ReliableStream(ReliableStream.Kind.BULK, connection, token, BULK_RECEIVE_LIMIT);
        }

        void close(long now) {
            if (!closing) {
                closing = true;
                closingDeadlineUs = now + CLOSING_US;
            }
        }

        boolean delivered() {
            return control.settled() && bulk.settled() && !control.acknowledgementPending()
                    && !bulk.acknowledgementPending();
        }
    }

    private final class ControlEndpoint implements FrameEndpoint {
        private final Association owner;
        private final Map<MessageType, Message> resident = new EnumMap<>(MessageType.class);
        private byte[] payload = new byte[256];
        private volatile int readTimeoutMillis;

        ControlEndpoint(Association owner) {
            this.owner = owner;
        }

        @Override
        public Message read(FrameContext context) throws IOException {
            Codec.FrameHeader frame = null;
            synchronized (lock) {
                final int timeout = readTimeoutMillis;
                final long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeout);
                while (frame == null) {
                    if (owner.broken != null) {
                        throw new IOException("the client broke the reliable channel: " + owner.broken);
                    }
                    if (owner.closing || association != owner) {
                        throw new EOFException();
                    }
                    final ReliableStream stream = owner.control;
                    final int available = stream.readableBytes();
                    if (available >= Constants.FRAME_HEADER_SIZE) {
                        final Codec.FrameHeader header = Codec.decodeFrameHeader(context, Direction.TO_SERVER,
                                stream.readBuffer(), stream.readOffset(), Constants.FRAME_HEADER_SIZE);
                        if (available - Constants.FRAME_HEADER_SIZE >= header.length) {
                            if (payload.length < header.length) {
                                payload = new byte[header.length];
                            }
                            System.arraycopy(stream.readBuffer(), stream.readOffset() + Constants.FRAME_HEADER_SIZE,
                                    payload, 0, header.length);
                            stream.consume(Constants.FRAME_HEADER_SIZE + header.length);
                            frame = header;
                            continue;
                        }
                    }
                    final long remaining = timeout == 0 ? 0 : deadline - System.nanoTime();
                    if (timeout != 0 && remaining <= 0) {
                        throw new SocketTimeoutException();
                    }
                    if (!waitNanos(remaining)) {
                        throw new IOException("interrupted");
                    }
                }
            }
            final Message message = context == FrameContext.CONTROL
                    ? resident.computeIfAbsent(frame.type, MessageType::create) : frame.type.create();
            return Codec.decodePayload(message, payload, 0, frame.length);
        }

        @Override
        public void write(Message message) throws IOException {
            final byte[] frame = Codec.encodeFrame(message);
            synchronized (lock) {
                if (owner.closing || association != owner) {
                    throw new IOException("the connection is closed");
                }
                owner.control.send(frame, 0, frame.length);
                lock.notifyAll();
            }
        }

        @Override
        public void setReadTimeout(int milliseconds) {
            readTimeoutMillis = milliseconds;
        }

        @Override
        public void close() {
            synchronized (lock) {
                owner.close(clock.getAsLong());
                lock.notifyAll();
            }
        }
    }

    private final class BulkEndpoint implements FrameEndpoint {
        private final Association owner;

        BulkEndpoint(Association owner) {
            this.owner = owner;
        }

        @Override
        public Message read(FrameContext context) throws IOException {
            throw new IOException("the bulk stream carries replies to the client only");
        }

        @Override
        public void write(Message message) throws IOException {
            final byte[] frame = Codec.encodeFrame(message);
            synchronized (lock) {
                if (owner.closing || association != owner) {
                    throw new IOException("the connection is closed");
                }
                owner.bulk.send(frame, 0, frame.length);
                lock.notifyAll();
            }
        }

        @Override
        public void setReadTimeout(int milliseconds) {
        }

        @Override
        public void close() {
        }
    }

    private final class MediaSlot {
        final Association owner;
        final int channel;
        MediaDatagrams endpoint;
        boolean closing;
        long nextClosedUs;
        long closingDeadlineUs;

        MediaSlot(Association owner, int channel) {
            this.owner = owner;
            this.channel = channel;
        }

        final class Request implements ChannelRequest {
            private boolean answered;

            @Override
            public ChannelId channel() {
                return channel == DirectMedia.VIDEO_CHANNEL ? ChannelId.VIDEO : ChannelId.AUDIO;
            }

            @Override
            public MediaEndpoint acceptMedia() throws IOException {
                final MediaDatagrams accepted;
                synchronized (lock) {
                    answer();
                    if (closed || owner.closing || association != owner) {
                        throw new IOException("the connection is gone");
                    }
                    accepted = new MediaDatagrams(MediaSlot.this);
                    endpoint = accepted;
                }
                sendOpened(owner, channel);
                return accepted;
            }

            @Override
            public FrameEndpoint acceptFrames() throws IOException {
                throw new IOException("over WiFi direct, bulk is a stream of the connection, not a channel to open");
            }

            @Override
            public void reject(ErrorCode code, String message) {
                synchronized (lock) {
                    answer();
                    if (owner.media[channel] == MediaSlot.this) {
                        owner.media[channel] = null;
                    }
                }
            }

            private void answer() {
                if (answered) {
                    throw new IllegalStateException("the channel request was answered already");
                }
                answered = true;
            }
        }
    }

    private final class MediaDatagrams implements MediaEndpoint {
        static final double MIN_PARITY = 0.02;
        static final double MAX_PARITY = 0.5;

        private final MediaSlot slot;
        private final DirectMedia.VideoSlicer slicer = new DirectMedia.VideoSlicer();
        private final byte[] audio = new byte[Reliable.DATAGRAM_LIMIT];
        private byte[] packet = new byte[16 * 1024];
        private byte[] previous = new byte[0];
        private int previousLength;
        private long number;
        private volatile boolean closed;
        private double lossRate = MIN_PARITY;
        private double parityRatio = 1.5 * MIN_PARITY + MIN_PARITY;
        private long shardsSent;

        MediaDatagrams(MediaSlot slot) {
            this.slot = slot;
        }

        @Override
        public void writePacket(byte[] header, ByteBuffer payload) throws IOException {
            final Association owner = slot.owner;
            synchronized (lock) {
                if (closed || UdpTransport.this.closed || owner.closing || association != owner) {
                    throw new IOException("the connection is gone");
                }
            }
            final int length = header.length + payload.remaining();
            if (packet.length < length) {
                packet = new byte[Math.max(length, 2 * packet.length)];
            }
            System.arraycopy(header, 0, packet, 0, header.length);
            payload.get(packet, header.length, length - header.length);
            if (slot.channel == DirectMedia.VIDEO_CHANNEL) {
                final double ratio = parityRatio();
                final int[] sent = {0};
                slicer.slice(owner.connection, number++, packet, 0, length, DirectMedia.recommendedShardSize(length),
                        (group, k) -> Math.min(ReedSolomon.MAX_SHARDS - k, Math.max(1, (int) Math.ceil(k * ratio))),
                        (datagram, size) -> {
                            send(datagram, size, owner.peer);
                            ++sent[0];
                        });
                countSent(sent[0]);
            } else {
                final int size = DirectMedia.writeAudio(audio, owner.connection, number++, packet, length,
                        previousLength > 0 ? previous : null, previousLength);
                send(audio, size, owner.peer);
                if (previous.length < length) {
                    previous = new byte[length];
                }
                System.arraycopy(packet, 0, previous, 0, length);
                previousLength = length;
            }
        }

        @Override
        public synchronized void feedback(long lostShards, long rebuiltShards) {
            if (shardsSent > 0) {
                final double loss = Math.min(1.0, (double) lostShards / shardsSent);
                lossRate = 0.8 * lossRate + 0.2 * loss;
                parityRatio = Math.max(MIN_PARITY, Math.min(MAX_PARITY, 1.5 * lossRate + MIN_PARITY));
            }
            shardsSent = 0;
        }

        private synchronized double parityRatio() {
            return parityRatio;
        }

        private synchronized void countSent(int count) {
            shardsSent += count;
        }

        @Override
        public void close() {
            closed = true;
            synchronized (lock) {
                final Association owner = slot.owner;
                if (owner.media[slot.channel] != slot || slot.closing) {
                    return;
                }
                if (UdpTransport.this.closed || owner.closing || association != owner) {
                    owner.media[slot.channel] = null;
                    return;
                }
                final long now = clock.getAsLong();
                slot.closing = true;
                slot.nextClosedUs = now;
                slot.closingDeadlineUs = now + CLOSED_GIVE_UP_US;
                lock.notifyAll();
            }
        }
    }

    private final class UdpSessionRequest implements SessionRequest {
        private final UdpHandshake.Request request;
        private Association accepted;

        UdpSessionRequest(UdpHandshake.Request request) {
            this.request = request;
        }

        @Override
        public ClientHello hello() {
            return request.hello();
        }

        @Override
        public boolean proven() {
            return request.proven();
        }

        @Override
        public FrameEndpoint accept(Welcome welcome) throws IOException {
            final byte[] frame = Codec.encodeFrame(welcome);
            synchronized (lock) {
                if (closed) {
                    throw new IOException("the transport is closed");
                }
                accepted = new Association(request.accept(), request.source());
                association = accepted;
                accepted.control.send(frame, 0, frame.length);
                lock.notifyAll();
                return accepted.endpoint;
            }
        }

        @Override
        public FrameEndpoint acceptedBulk() {
            synchronized (lock) {
                return accepted != null ? accepted.bulkEndpoint : null;
            }
        }

        @Override
        public void reject(ErrorCode code, String message) {
            request.reject(code, message);
        }
    }
}
