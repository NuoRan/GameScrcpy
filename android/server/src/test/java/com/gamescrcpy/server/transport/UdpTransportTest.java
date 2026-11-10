package com.gamescrcpy.server.transport;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Datagram;
import com.gamescrcpy.protocol.DirectMedia;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.AppList;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.Goodbye;
import com.gamescrcpy.protocol.Messages.HandshakeError;
import com.gamescrcpy.protocol.Messages.Ping;
import com.gamescrcpy.protocol.Messages.Pong;
import com.gamescrcpy.protocol.Messages.ServerHello;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.protocol.Proofs;
import com.gamescrcpy.protocol.Reliable;
import com.gamescrcpy.server.session.DeviceDescriptor;
import com.gamescrcpy.server.session.SessionServer;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class UdpTransportTest {

    private static final byte[] TOKEN = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    private static final InetAddress LOOPBACK = InetAddress.getLoopbackAddress();

    private static long nowUs() {
        return System.nanoTime() / 1000;
    }

    private static final class Client implements AutoCloseable {
        final DatagramSocket socket = new DatagramSocket(0, LOOPBACK);
        final InetSocketAddress server;
        final Random random = new Random(42);
        final byte[] buffer = new byte[Datagram.MAX_SIZE + 1];
        double loss;
        long connection;
        ReliableStream control;

        Client(int port) throws IOException {
            server = new InetSocketAddress(LOOPBACK, port);
        }

        void sendRaw(byte[] data, int length) throws IOException {
            if (random.nextDouble() >= loss) {
                socket.send(new DatagramPacket(data, length, server));
            }
        }

        DatagramPacket receiveRaw(long timeoutUs) throws IOException {
            socket.setSoTimeout((int) Math.max(1, Math.min(timeoutUs / 1000, 1000)));
            final DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
            try {
                socket.receive(packet);
            } catch (SocketTimeoutException e) {
                return null;
            }
            return random.nextDouble() < loss ? null : packet;
        }

        HandshakeError handshake(byte[] token) throws IOException {
            final byte[] knock = new byte[Datagram.HEADER_SIZE];
            Datagram.writeHeader(knock, 0, Datagram.HANDSHAKE_CHANNEL, 0);
            ServerHello hello = null;
            for (int attempt = 0; attempt < 8 && hello == null; ++attempt) {
                sendRaw(knock, knock.length);
                final DatagramPacket packet = receiveRaw(250_000);
                if (packet != null) {
                    hello = (ServerHello) handshakeMessage(packet);
                }
            }
            assertTrue(hello != null, "no ServerHello");
            final ClientHello request = new ClientHello();
            request.protocolVersion = Constants.PROTOCOL_VERSION;
            request.clientVersion = "test";
            request.proof = Proofs.udpHello(token, hello.nonce);
            final byte[] frame = Codec.encodeFrame(request);
            final byte[] datagram = new byte[Datagram.HEADER_SIZE + frame.length];
            Datagram.writeHeader(datagram, 0, Datagram.HANDSHAKE_CHANNEL, 0);
            System.arraycopy(frame, 0, datagram, Datagram.HEADER_SIZE, frame.length);
            for (int attempt = 0; attempt < 8; ++attempt) {
                sendRaw(datagram, datagram.length);
                final DatagramPacket packet = receiveRaw(250_000);
                if (packet == null) {
                    continue;
                }
                final Datagram.Header header = Datagram.decodeHeader(buffer, 0, packet.getLength());
                if (packet.getLength() == Datagram.HEADER_SIZE) {
                    connection = header.connection;
                    control = new ReliableStream(ReliableStream.Kind.CONTROL, connection, token, 1 << 20);
                    return null;
                }
                final Message answer = handshakeMessage(packet);
                if (answer instanceof ServerHello) {
                    continue;
                }
                return assertInstanceOf(HandshakeError.class, answer);
            }
            fail("no answer to the ClientHello");
            return null;
        }

        private Message handshakeMessage(DatagramPacket packet) {
            final Datagram.Header header = Datagram.decodeHeader(buffer, 0, packet.getLength());
            assertEquals(Datagram.HANDSHAKE_CHANNEL, header.channel);
            return Codec.decodeFrame(FrameContext.HANDSHAKE, Direction.TO_CLIENT, buffer, Datagram.HEADER_SIZE,
                    packet.getLength() - Datagram.HEADER_SIZE);
        }

        void write(Message message) {
            final byte[] frame = Codec.encodeFrame(message);
            control.send(frame, 0, frame.length);
        }

        Message read(FrameContext context, long timeoutMillis) throws IOException {
            final long until = nowUs() + timeoutMillis * 1000;
            while (true) {
                final int available = control.readableBytes();
                if (available >= Constants.FRAME_HEADER_SIZE) {
                    final Codec.FrameHeader header = Codec.decodeFrameHeader(context, Direction.TO_CLIENT,
                            control.readBuffer(), control.readOffset(), Constants.FRAME_HEADER_SIZE);
                    if (available - Constants.FRAME_HEADER_SIZE >= header.length) {
                        final Message message = Codec.decodePayload(header.type.create(), control.readBuffer(),
                                control.readOffset() + Constants.FRAME_HEADER_SIZE, header.length);
                        control.consume(Constants.FRAME_HEADER_SIZE + header.length);
                        return message;
                    }
                }
                final long now = nowUs();
                if (now >= until) {
                    return null;
                }
                pump(Math.min(until, now + 20_000));
            }
        }

        void pump(long untilUs) throws IOException {
            for (int length = control.poll(nowUs()); length != 0; length = control.poll(nowUs())) {
                sendRaw(control.datagram(), length);
            }
            final long wake = Math.min(untilUs, Math.max(control.deadline(), nowUs()));
            final DatagramPacket packet = receiveRaw(Math.max(1_000, wake - nowUs()));
            if (packet != null) {
                final Datagram.Header header = Datagram.decodeHeader(buffer, 0, packet.getLength());
                if (header.channel == Reliable.CONTROL_CHANNEL) {
                    assertNotEquals(ReliableStream.Verdict.BROKEN, control.receive(buffer, packet.getLength(), nowUs()));
                }
            }
        }

        boolean settle(long timeoutMillis) throws IOException {
            final long until = nowUs() + timeoutMillis * 1000;
            while (!control.settled() && nowUs() < until) {
                pump(Math.min(until, nowUs() + 20_000));
            }
            return control.settled();
        }

        @Override
        public void close() {
            socket.close();
        }
    }

    private static final class QuietHandler implements SessionServer.SessionHandler {
        final CountDownLatch lost = new CountDownLatch(1);
        final BlockingQueue<MediaEndpoint> media = new LinkedBlockingQueue<>();
        final BlockingQueue<FrameEndpoint> bulk = new LinkedBlockingQueue<>();

        @Override public void handle(Message message, FrameEndpoint control) { }
        @Override public void connectionLost() { lost.countDown(); }
        @Override public void channelOpened(ChannelId channel, MediaEndpoint endpoint) { media.add(endpoint); }
        @Override public void bulkOpened(FrameEndpoint endpoint) { bulk.add(endpoint); }
    }

    private static List<byte[]> datagramsOn(Client client, int channel, int count) throws IOException {
        final List<byte[]> out = new ArrayList<>();
        final long until = nowUs() + 3_000_000;
        while (out.size() < count && nowUs() < until) {
            final DatagramPacket packet = client.receiveRaw(100_000);
            if (packet != null && Datagram.decodeHeader(client.buffer, 0, packet.getLength()).channel == channel) {
                out.add(Arrays.copyOf(client.buffer, packet.getLength()));
            }
        }
        return out;
    }

    private static List<byte[]> datagramsWithin(Client client, int channel, long micros) throws IOException {
        final List<byte[]> out = new ArrayList<>();
        final long until = nowUs() + micros;
        while (nowUs() < until) {
            final DatagramPacket packet = client.receiveRaw(Math.max(1_000, until - nowUs()));
            if (packet != null && Datagram.decodeHeader(client.buffer, 0, packet.getLength()).channel == channel) {
                out.add(Arrays.copyOf(client.buffer, packet.getLength()));
            }
        }
        return out;
    }

    private MediaEndpoint openVideo(Client client) throws Exception {
        sendOpen(client, DirectMedia.VIDEO_CHANNEL);
        assertEquals(Datagram.HEADER_SIZE, datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 1).get(0).length);
        final MediaEndpoint video = handler.media.poll(2, TimeUnit.SECONDS);
        assertNotNull(video);
        return video;
    }

    private static void sendOpen(Client client, int channel) throws IOException {
        final byte[] open = new byte[Datagram.HEADER_SIZE];
        Datagram.writeHeader(open, 0, channel, client.connection);
        client.sendRaw(open, open.length);
    }

    private final QuietHandler handler = new QuietHandler();
    private UdpTransport transport;
    private CompletableFuture<SessionServer.Outcome> outcome;

    private int start(SessionServer.Config config) throws IOException {
        transport = new UdpTransport(new DatagramSocket(0, LOOPBACK), TOKEN, new SecureRandom(), UdpTransportTest::nowUs);
        final DeviceDescriptor device = () -> {
            final Welcome welcome = new Welcome();
            welcome.model = "Test Phone";
            welcome.sdkInt = 35;
            return welcome;
        };
        final AtomicLong clock = new AtomicLong(1_000_000);
        final SessionServer server = new SessionServer(transport, device, handler, config, clock::getAndIncrement);
        outcome = CompletableFuture.supplyAsync(server::run);
        return transport.port();
    }

    private static SessionServer.Config config() {
        final SessionServer.Config config = new SessionServer.Config();
        config.firstConnectionTimeoutMillis = 5_000;
        config.lingerMillis = 3_000;
        return config;
    }

    @AfterEach
    void stop() {
        if (transport != null) {
            transport.close();
        }
    }

    private static Ping ping(long nonce) {
        final Ping ping = new Ping();
        ping.nonce = nonce;
        ping.clientTimeUs = 1;
        return ping;
    }

    @Test
    void aProvenClientIsWelcomedOnTheControlStreamAndAnswered() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            final Welcome welcome = assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
            assertEquals("Test Phone", welcome.model);
            assertFalse(welcome.resumed);

            client.write(ping(7));
            assertEquals(7, assertInstanceOf(Pong.class, client.read(FrameContext.CONTROL, 2_000)).nonce);
            client.write(new Goodbye());
            final long before = nowUs();
            assertTrue(client.settle(2_000));
            assertTrue(nowUs() - before < 500_000);
            assertEquals(SessionServer.Outcome.GOODBYE, outcome.get(5, TimeUnit.SECONDS));
        }
    }

    @Test
    void theBulkStreamOfTheConnectionCarriesRepliesWithoutBeingOpened() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
            final FrameEndpoint bulk = handler.bulk.poll(2, TimeUnit.SECONDS);
            assertNotNull(bulk);
            final AppList list = new AppList();
            list.requestId = 11;
            bulk.write(list);

            final ReliableStream stream = new ReliableStream(ReliableStream.Kind.BULK, client.connection, TOKEN, 1 << 20);
            final long until = nowUs() + 3_000_000;
            while (stream.readableBytes() < Constants.FRAME_HEADER_SIZE && nowUs() < until) {
                final DatagramPacket packet = client.receiveRaw(100_000);
                if (packet != null
                        && Datagram.decodeHeader(client.buffer, 0, packet.getLength()).channel == Reliable.BULK_CHANNEL) {
                    assertNotEquals(ReliableStream.Verdict.BROKEN, stream.receive(client.buffer, packet.getLength(), nowUs()));
                }
            }
            final Codec.FrameHeader header = Codec.decodeFrameHeader(FrameContext.BULK, Direction.TO_CLIENT,
                    stream.readBuffer(), stream.readOffset(), Constants.FRAME_HEADER_SIZE);
            final Message reply = Codec.decodePayload(header.type.create(), stream.readBuffer(),
                    stream.readOffset() + Constants.FRAME_HEADER_SIZE, header.length);
            assertEquals(11, assertInstanceOf(AppList.class, reply).requestId);
        }
    }

    @Test
    void aWrongTokenIsRefusedInTheHandshakeAndTheServerWaitsOn() throws Exception {
        final int port = start(config());
        final byte[] wrong = TOKEN.clone();
        wrong[0] ^= 1;
        try (Client intruder = new Client(port)) {
            final HandshakeError refusal = intruder.handshake(wrong);
            assertEquals(ErrorCode.AUTHENTICATION_FAILED, assertInstanceOf(HandshakeError.class, refusal).code);
        }
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
        }
    }

    @Test
    void underTenPercentLossBothWaysEveryPingIsAnsweredInOrder() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
            client.loss = 0.10;
            final int count = 200;
            for (int i = 0; i < count; ++i) {
                client.write(ping(i));
            }
            for (int i = 0; i < count; ++i) {
                final Message reply = client.read(FrameContext.CONTROL, 10_000);
                assertEquals(i, assertInstanceOf(Pong.class, reply, "pong " + i).nonce);
            }
            client.write(new Goodbye());
            client.settle(5_000);
            assertEquals(SessionServer.Outcome.GOODBYE, outcome.get(10, TimeUnit.SECONDS));
        }
    }

    @Test
    void anAuthenticDatagramFromAnotherAddressIsIgnored() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port); DatagramSocket stranger = new DatagramSocket(0, LOOPBACK)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));

            final byte[] frame = Codec.encodeFrame(ping(99));
            final Reliable.Writer writer = new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT]);
            writer.startControl(client.connection, 1_000, 1, 0).add(0, frame, 0, frame.length);
            final int length = writer.finish(TOKEN);
            stranger.send(new DatagramPacket(writer.buffer(), length, new InetSocketAddress(LOOPBACK, port)));
            stranger.setSoTimeout(300);
            final DatagramPacket reply = new DatagramPacket(new byte[Datagram.MAX_SIZE], Datagram.MAX_SIZE);
            try {
                stranger.receive(reply);
                fail("the server answered a stranger");
            } catch (SocketTimeoutException expected) {
            }

            client.write(ping(5));
            assertEquals(5, assertInstanceOf(Pong.class, client.read(FrameContext.CONTROL, 2_000)).nonce);
        }
    }

    @Test
    void aMediaChannelOpensWithAnEmptyDatagramAndCarriesVideoInShards() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));

            sendOpen(client, DirectMedia.VIDEO_CHANNEL);
            final List<byte[]> opened = datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 1);
            assertEquals(1, opened.size());
            assertEquals(Datagram.HEADER_SIZE, opened.get(0).length);
            final MediaEndpoint video = handler.media.poll(2, TimeUnit.SECONDS);
            assertNotNull(video);
            sendOpen(client, DirectMedia.VIDEO_CHANNEL);
            assertEquals(Datagram.HEADER_SIZE, datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 1).get(0).length);
            assertNull(handler.media.poll(200, TimeUnit.MILLISECONDS));

            final byte[] header = new byte[20];
            header[0] = 2;
            final byte[] payload = new byte[5000];
            new Random(3).nextBytes(payload);
            video.writePacket(header, ByteBuffer.wrap(payload));
            final List<byte[]> shards = datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 6);
            assertEquals(6, shards.size());
            final ByteArrayOutputStream rebuilt = new ByteArrayOutputStream();
            for (byte[] datagram : shards) {
                final DirectMedia.VideoShard shard = DirectMedia.decodeVideoShard(datagram, datagram.length);
                assertEquals(0, shard.packet);
                assertEquals(5020, shard.packetSize);
                assertEquals(1, shard.m);
                if (shard.index < shard.k) {
                    rebuilt.write(datagram, shard.offset, shard.size);
                }
            }
            final byte[] expected = new byte[5020];
            System.arraycopy(header, 0, expected, 0, 20);
            System.arraycopy(payload, 0, expected, 20, 5000);
            assertArrayEquals(expected, rebuilt.toByteArray());

            video.feedback(6, 0);
            video.writePacket(header, ByteBuffer.wrap(payload));
            final List<byte[]> more = datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 7);
            assertEquals(7, more.size());
            assertEquals(2, DirectMedia.decodeVideoShard(more.get(0), more.get(0).length).m);
            assertEquals(1, DirectMedia.decodeVideoShard(more.get(0), more.get(0).length).packet);
        }
    }

    @Test
    void aMediaChannelTheSessionClosesSaysClosedUntilTheClientAcknowledges() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
            final MediaEndpoint video = openVideo(client);

            video.close();
            final long closedAt = nowUs();
            final List<byte[]> closed = datagramsOn(client, DirectMedia.VIDEO_CHANNEL, 2);
            assertEquals(2, closed.size());
            for (byte[] datagram : closed) {
                assertTrue(DirectMedia.isClosed(datagram, datagram.length));
            }
            assertTrue(nowUs() - closedAt >= 200_000);
            assertThrows(IOException.class, () -> video.writePacket(new byte[20], ByteBuffer.wrap(new byte[1])));

            final byte[] acknowledgement = new byte[DirectMedia.CLOSED_SIZE];
            client.sendRaw(acknowledgement,
                    DirectMedia.writeClosed(acknowledgement, DirectMedia.VIDEO_CHANNEL, client.connection));
            datagramsWithin(client, DirectMedia.VIDEO_CHANNEL, 100_000);
            assertTrue(datagramsWithin(client, DirectMedia.VIDEO_CHANNEL, 600_000).isEmpty());

            openVideo(client);
        }
    }

    @Test
    void anUnacknowledgedClosedIsGivenUpAfterOneAndAHalfSeconds() throws Exception {
        final int port = start(config());
        try (Client client = new Client(port)) {
            assertNull(client.handshake(TOKEN));
            assertInstanceOf(Welcome.class, client.read(FrameContext.HANDSHAKE, 2_000));
            final MediaEndpoint video = openVideo(client);

            video.close();
            final List<byte[]> closed = datagramsWithin(client, DirectMedia.VIDEO_CHANNEL, 2_200_000);
            assertTrue(closed.size() >= 5 && closed.size() <= 7, "\"closed\" sent " + closed.size() + " times");
            for (byte[] datagram : closed) {
                assertTrue(DirectMedia.isClosed(datagram, datagram.length));
            }
            openVideo(client);
        }
    }

    @Test
    void theClientBackFromAnotherAddressTakesOverAtOnce() throws Exception {
        final SessionServer.Config config = config();
        config.lingerMillis = 0;
        final int port = start(config);
        try (Client first = new Client(port); Client second = new Client(port)) {
            assertNull(first.handshake(TOKEN));
            assertFalse(assertInstanceOf(Welcome.class, first.read(FrameContext.HANDSHAKE, 2_000)).resumed);
            final long before = nowUs();
            assertNull(second.handshake(TOKEN));
            assertTrue(assertInstanceOf(Welcome.class, second.read(FrameContext.HANDSHAKE, 2_000)).resumed);
            assertTrue(nowUs() - before < 1_000_000);
            assertTrue(handler.lost.await(1, TimeUnit.SECONDS));

            second.write(ping(4));
            assertEquals(4, assertInstanceOf(Pong.class, second.read(FrameContext.CONTROL, 2_000)).nonce);
            first.write(ping(5));
            assertNull(first.read(FrameContext.CONTROL, 500));
            second.write(new Goodbye());
            second.settle(2_000);
            assertEquals(SessionServer.Outcome.GOODBYE, outcome.get(5, TimeUnit.SECONDS));
        }
    }

    @Test
    void aClientComingBackAfterTheHeartbeatTimeoutResumesTheSession() throws Exception {
        final SessionServer.Config config = config();
        config.heartbeatTimeoutMillis = 300;
        final int port = start(config);
        try (Client first = new Client(port)) {
            assertNull(first.handshake(TOKEN));
            assertFalse(assertInstanceOf(Welcome.class, first.read(FrameContext.HANDSHAKE, 2_000)).resumed);
        }
        assertTrue(handler.lost.await(3, TimeUnit.SECONDS));
        try (Client second = new Client(port)) {
            assertNull(second.handshake(TOKEN));
            assertTrue(assertInstanceOf(Welcome.class, second.read(FrameContext.HANDSHAKE, 2_000)).resumed);
            second.write(ping(3));
            assertEquals(3, assertInstanceOf(Pong.class, second.read(FrameContext.CONTROL, 2_000)).nonce);
        }
    }
}
