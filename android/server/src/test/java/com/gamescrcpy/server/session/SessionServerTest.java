package com.gamescrcpy.server.session;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.AppList;
import com.gamescrcpy.protocol.Messages.ChannelAccepted;
import com.gamescrcpy.protocol.Messages.ChannelOpen;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.Goodbye;
import com.gamescrcpy.protocol.Messages.HandshakeError;
import com.gamescrcpy.protocol.Messages.Ping;
import com.gamescrcpy.protocol.Messages.Pong;
import com.gamescrcpy.protocol.Messages.ServerGoodbye;
import com.gamescrcpy.protocol.Messages.ServerHello;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.protocol.Proofs;
import com.gamescrcpy.server.transport.Connection;
import com.gamescrcpy.server.transport.FrameEndpoint;
import com.gamescrcpy.server.transport.Listener;
import com.gamescrcpy.server.transport.SessionRequest;
import com.gamescrcpy.server.transport.SessionTransport;
import com.gamescrcpy.server.transport.TunnelTransport;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.security.SecureRandom;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class SessionServerTest {

    private static final byte[] TOKEN = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    private static final class TcpListener implements Listener {
        final ServerSocket server = new ServerSocket(0, 8, InetAddress.getLoopbackAddress());

        TcpListener() throws IOException {
        }

        @Override
        public Connection accept() throws IOException {
            final Socket socket = server.accept();
            return new Connection() {
                @Override public InputStream input() throws IOException { return socket.getInputStream(); }
                @Override public OutputStream output() throws IOException { return socket.getOutputStream(); }
                @Override public void setReadTimeout(int milliseconds) throws IOException { socket.setSoTimeout(milliseconds); }
                @Override public void close() throws IOException { socket.close(); }
            };
        }

        @Override
        public void close() throws IOException {
            server.close();
        }
    }

    private static final class Client implements AutoCloseable {
        final Socket socket;
        final DataInputStream input;
        final ServerHello hello;

        Client(int port) throws IOException {
            socket = new Socket(InetAddress.getLoopbackAddress(), port);
            socket.setSoTimeout(5_000);
            input = new DataInputStream(socket.getInputStream());
            hello = (ServerHello) read(FrameContext.HANDSHAKE);
        }

        Message read(FrameContext context) throws IOException {
            final byte[] header = new byte[Constants.FRAME_HEADER_SIZE];
            input.readFully(header);
            final Codec.FrameHeader frame = Codec.decodeFrameHeader(context, Direction.TO_CLIENT, header, 0, header.length);
            final byte[] payload = new byte[frame.length];
            input.readFully(payload);
            return Codec.decodePayload(frame.type.create(), payload, 0, payload.length);
        }

        void write(Message message) throws IOException {
            socket.getOutputStream().write(Codec.encodeFrame(message));
        }

        void writeRaw(byte[] bytes) throws IOException {
            socket.getOutputStream().write(bytes);
        }

        Message clientHello(int version, byte[] token) throws IOException {
            final ClientHello request = new ClientHello();
            request.protocolVersion = version;
            request.clientVersion = "test";
            request.proof = Proofs.hello(token, hello.nonce);
            write(request);
            return read(FrameContext.HANDSHAKE);
        }

        Message channelOpen(ChannelId channel, byte[] token) throws IOException {
            final ChannelOpen request = new ChannelOpen();
            request.channel = channel;
            request.proof = Proofs.channel(token, hello.nonce, channel.wireValue());
            write(request);
            return read(FrameContext.HANDSHAKE);
        }

        boolean closedByServer() {
            try {
                return input.read() < 0;
            } catch (IOException e) {
                return true;
            }
        }

        @Override
        public void close() throws IOException {
            socket.close();
        }
    }

    private static final class RecordingHandler implements SessionServer.SessionHandler {
        final CountDownLatch lost = new CountDownLatch(1);
        final BlockingQueue<FrameEndpoint> bulk = new LinkedBlockingQueue<>();

        @Override public void handle(Message message, FrameEndpoint control) { }
        @Override public void connectionLost() { lost.countDown(); }
        @Override public void bulkOpened(FrameEndpoint endpoint) { bulk.add(endpoint); }
    }

    private TcpListener listener;
    private final RecordingHandler handler = new RecordingHandler();
    private final AtomicLong clock = new AtomicLong(1_000_000);
    private CompletableFuture<SessionServer.Outcome> outcome;

    private void start(SessionServer.Config config) throws IOException {
        listener = new TcpListener();
        final DeviceDescriptor device = () -> {
            final Welcome welcome = new Welcome();
            welcome.model = "Test Phone";
            welcome.sdkInt = 35;
            return welcome;
        };
        final TunnelTransport transport = new TunnelTransport(listener, TOKEN, TunnelTransport.HANDSHAKE_TIMEOUT_MILLIS,
                new SecureRandom());
        final SessionServer server = new SessionServer(transport, device, handler, config, clock::getAndIncrement);
        outcome = CompletableFuture.supplyAsync(server::run);
    }

    private static SessionServer.Config fastConfig() {
        final SessionServer.Config config = new SessionServer.Config();
        config.firstConnectionTimeoutMillis = 5_000;
        config.lingerMillis = 2_000;
        return config;
    }

    private SessionServer.Outcome awaitOutcome() throws Exception {
        return outcome.get(10, TimeUnit.SECONDS);
    }

    private int port() {
        return listener.server.getLocalPort();
    }

    @AfterEach
    void stop() throws IOException {
        if (listener != null) {
            listener.close();
        }
    }

    @Test
    void aValidHandshakeWelcomesTheClient() throws Exception {
        start(fastConfig());
        try (Client client = new Client(port())) {
            assertEquals(Constants.PROTOCOL_VERSION, client.hello.protocolVersion);
            final Welcome welcome = assertInstanceOf(Welcome.class, client.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            assertEquals("Test Phone", welcome.model);
            assertFalse(welcome.resumed);
            client.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void aWrongTokenIsRejectedAndTheServerKeepsWaiting() throws Exception {
        start(fastConfig());
        try (Client intruder = new Client(port())) {
            final byte[] wrong = TOKEN.clone();
            wrong[0] ^= 1;
            final HandshakeError error = assertInstanceOf(HandshakeError.class,
                    intruder.clientHello(Constants.PROTOCOL_VERSION, wrong));
            assertEquals(ErrorCode.AUTHENTICATION_FAILED, error.code);
            assertTrue(intruder.closedByServer());
        }
        try (Client client = new Client(port())) {
            assertInstanceOf(Welcome.class, client.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            client.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void aProtocolVersionMismatchFailsWithAReadableMessage() throws Exception {
        start(fastConfig());
        try (Client client = new Client(port())) {
            final HandshakeError error = assertInstanceOf(HandshakeError.class, client.clientHello(2, TOKEN));
            assertEquals(ErrorCode.VERSION_MISMATCH, error.code);
            assertTrue(error.message.contains("client protocol 2"), error.message);
            assertTrue(error.message.contains("server protocol 1"), error.message);
        }
        listener.close();
    }

    @Test
    void channelsOpenOnlyInsideASessionAndWithAValidProof() throws Exception {
        start(fastConfig());
        try (Client early = new Client(port())) {
            final HandshakeError error = assertInstanceOf(HandshakeError.class, early.channelOpen(ChannelId.VIDEO, TOKEN));
            assertEquals(ErrorCode.UNEXPECTED_MESSAGE, error.code);
        }
        try (Client control = new Client(port())) {
            assertInstanceOf(Welcome.class, control.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            try (Client video = new Client(port())) {
                final ChannelAccepted accepted = assertInstanceOf(ChannelAccepted.class,
                        video.channelOpen(ChannelId.VIDEO, TOKEN));
                assertEquals(ChannelId.VIDEO, accepted.channel);
            }
            try (Client forged = new Client(port())) {
                final byte[] wrong = TOKEN.clone();
                wrong[15] ^= 1;
                final HandshakeError error = assertInstanceOf(HandshakeError.class,
                        forged.channelOpen(ChannelId.AUDIO, wrong));
                assertEquals(ErrorCode.AUTHENTICATION_FAILED, error.code);
            }
            control.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void anOpenedBulkChannelCarriesRepliesToTheClient() throws Exception {
        start(fastConfig());
        try (Client control = new Client(port())) {
            assertInstanceOf(Welcome.class, control.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            assertNull(handler.bulk.poll(100, TimeUnit.MILLISECONDS), "a bulk channel appeared without being opened");
            try (Client bulk = new Client(port())) {
                assertEquals(ChannelId.BULK,
                        assertInstanceOf(ChannelAccepted.class, bulk.channelOpen(ChannelId.BULK, TOKEN)).channel);
                final FrameEndpoint endpoint = handler.bulk.poll(5, TimeUnit.SECONDS);
                assertNotNull(endpoint);
                final AppList list = new AppList();
                list.requestId = 5;
                endpoint.write(list);
                assertEquals(5, assertInstanceOf(AppList.class, bulk.read(FrameContext.BULK)).requestId);
            }
            control.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void whileASessionIsActiveAnUnprovenHandshakeIsRefusedAndTheSessionGoesOn() throws Exception {
        start(fastConfig());
        try (Client control = new Client(port())) {
            assertInstanceOf(Welcome.class, control.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            final byte[] wrong = TOKEN.clone();
            wrong[15] ^= 1;
            try (Client intruder = new Client(port())) {
                final HandshakeError error = assertInstanceOf(HandshakeError.class,
                        intruder.clientHello(Constants.PROTOCOL_VERSION, wrong));
                assertEquals(ErrorCode.UNEXPECTED_MESSAGE, error.code);
            }
            try (Client older = new Client(port())) {
                final HandshakeError error = assertInstanceOf(HandshakeError.class, older.clientHello(2, TOKEN));
                assertEquals(ErrorCode.UNEXPECTED_MESSAGE, error.code);
            }
            final Ping ping = new Ping();
            ping.nonce = 9;
            control.write(ping);
            assertEquals(9, assertInstanceOf(Pong.class, control.read(FrameContext.CONTROL)).nonce);
            assertEquals(1, handler.lost.getCount());
            control.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void theClientBackOnANewConnectionTakesOverAsAResumption() throws Exception {
        start(fastConfig());
        try (Client first = new Client(port())) {
            assertFalse(assertInstanceOf(Welcome.class, first.clientHello(Constants.PROTOCOL_VERSION, TOKEN)).resumed);
            try (Client second = new Client(port())) {
                final Welcome welcome = assertInstanceOf(Welcome.class,
                        second.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
                assertTrue(welcome.resumed);
                assertTrue(handler.lost.await(5, TimeUnit.SECONDS));
                assertTrue(first.closedByServer());
                second.write(new Goodbye());
            }
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    private static final Message CLOSED = new Ping();

    private static final class QueueEndpoint implements FrameEndpoint {
        private final BlockingQueue<Message> inbox = new LinkedBlockingQueue<>();
        private final AtomicBoolean closed = new AtomicBoolean();

        @Override
        public Message read(FrameContext context) throws IOException {
            final Message message;
            try {
                message = inbox.take();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException(e);
            }
            if (message == CLOSED) {
                throw new IOException("closed");
            }
            return message;
        }

        @Override public void write(Message message) { }
        @Override public void setReadTimeout(int milliseconds) { }

        @Override
        public void close() {
            closed.set(true);
            inbox.add(CLOSED);
        }
    }

    private static SessionRequest requestFor(List<Welcome> welcomes, FrameEndpoint endpoint, Runnable whileWelcoming) {
        final ClientHello hello = new ClientHello();
        hello.protocolVersion = Constants.PROTOCOL_VERSION;
        return new SessionRequest() {
            @Override public ClientHello hello() { return hello; }
            @Override public boolean proven() { return true; }
            @Override public void reject(ErrorCode code, String message) { }

            @Override
            public FrameEndpoint accept(Welcome welcome) {
                welcomes.add(welcome);
                if (whileWelcoming != null) {
                    whileWelcoming.run();
                }
                return endpoint;
            }
        };
    }

    @Test
    void aClientBackWhileTheFirstIsStillBeingWelcomedTakesOverAtOnce() throws Exception {
        final List<Welcome> welcomes = new CopyOnWriteArrayList<>();
        final QueueEndpoint firstControl = new QueueEndpoint();
        final QueueEndpoint secondControl = new QueueEndpoint();
        secondControl.inbox.add(new Goodbye());
        final SessionTransport.Events[] events = new SessionTransport.Events[1];
        final SessionRequest second = requestFor(welcomes, secondControl, null);
        final SessionRequest first = requestFor(welcomes, firstControl, () -> events[0].sessionRequested(second));
        final SessionTransport transport = new SessionTransport() {
            @Override
            public void start(Events started) {
                events[0] = started;
                started.sessionRequested(first);
            }

            @Override public void close() { }
        };
        final SessionServer server = new SessionServer(transport, Welcome::new, handler, fastConfig(),
                clock::getAndIncrement);
        final CompletableFuture<SessionServer.Outcome> ran = CompletableFuture.supplyAsync(server::run);
        assertEquals(SessionServer.Outcome.GOODBYE, ran.get(10, TimeUnit.SECONDS));
        assertEquals(2, welcomes.size());
        assertFalse(welcomes.get(0).resumed);
        assertTrue(welcomes.get(1).resumed);
        assertTrue(firstControl.closed.get());
        assertTrue(handler.lost.await(1, TimeUnit.SECONDS));
    }

    @Test
    void pingsAreAnsweredWithTheServerClock() throws Exception {
        start(fastConfig());
        try (Client client = new Client(port())) {
            client.clientHello(Constants.PROTOCOL_VERSION, TOKEN);
            final Ping ping = new Ping();
            ping.nonce = 7;
            ping.clientTimeUs = 123;
            client.write(ping);
            final Pong first = assertInstanceOf(Pong.class, client.read(FrameContext.CONTROL));
            client.write(ping);
            final Pong second = assertInstanceOf(Pong.class, client.read(FrameContext.CONTROL));
            assertEquals(7, first.nonce);
            assertEquals(123, first.clientTimeUs);
            assertTrue(second.serverTimeUs > first.serverTimeUs);
            client.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void aLostConnectionLingersAndTheSameTokenResumesIt() throws Exception {
        start(fastConfig());
        try (Client client = new Client(port())) {
            client.clientHello(Constants.PROTOCOL_VERSION, TOKEN);
        }
        assertTrue(handler.lost.await(5, TimeUnit.SECONDS));
        try (Client resumed = new Client(port())) {
            final Welcome welcome = assertInstanceOf(Welcome.class, resumed.clientHello(Constants.PROTOCOL_VERSION, TOKEN));
            assertTrue(welcome.resumed);
            resumed.write(new Goodbye());
        }
        assertEquals(SessionServer.Outcome.GOODBYE, awaitOutcome());
    }

    @Test
    void lingeringEndsWhenNobodyComesBack() throws Exception {
        final SessionServer.Config config = fastConfig();
        config.lingerMillis = 200;
        start(config);
        try (Client client = new Client(port())) {
            client.clientHello(Constants.PROTOCOL_VERSION, TOKEN);
        }
        assertEquals(SessionServer.Outcome.LINGER_EXPIRED, awaitOutcome());
    }

    @Test
    void silenceBeyondTheHeartbeatTimeoutCountsAsALostConnection() throws Exception {
        final SessionServer.Config config = fastConfig();
        config.heartbeatTimeoutMillis = 200;
        config.lingerMillis = 200;
        start(config);
        try (Client client = new Client(port())) {
            client.clientHello(Constants.PROTOCOL_VERSION, TOKEN);
            assertTrue(handler.lost.await(5, TimeUnit.SECONDS));
            assertEquals(SessionServer.Outcome.LINGER_EXPIRED, awaitOutcome());
        }
    }

    @Test
    void aMalformedControlFrameEndsTheSessionWithAReason() throws Exception {
        start(fastConfig());
        try (Client client = new Client(port())) {
            client.clientHello(Constants.PROTOCOL_VERSION, TOKEN);
            final ByteArrayOutputStream unknownType = new ByteArrayOutputStream();
            unknownType.write(new byte[] {0x01, (byte) 0xFF, 0, 0, 0, 0});
            client.writeRaw(unknownType.toByteArray());
            final ServerGoodbye goodbye = assertInstanceOf(ServerGoodbye.class, client.read(FrameContext.CONTROL));
            assertEquals(ErrorCode.UNKNOWN_TYPE, goodbye.reason);
        }
        assertEquals(SessionServer.Outcome.PROTOCOL_ERROR, awaitOutcome());
    }

    @Test
    void theServerGivesUpWhenNobodyConnects() throws Exception {
        final SessionServer.Config config = fastConfig();
        config.firstConnectionTimeoutMillis = 200;
        start(config);
        assertEquals(SessionServer.Outcome.NO_CLIENT, awaitOutcome());
    }
}
