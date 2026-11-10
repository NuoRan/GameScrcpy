package com.gamescrcpy.server.transport;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Datagram;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ChannelOpen;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.HandshakeError;
import com.gamescrcpy.protocol.Messages.ServerHello;
import com.gamescrcpy.protocol.Proofs;
import java.net.InetSocketAddress;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.jupiter.api.Test;

class UdpHandshakeTest {

    private static final byte[] TOKEN = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    private static final InetSocketAddress CLIENT = new InetSocketAddress("192.168.1.20", 40000);
    private static final InetSocketAddress ROAMED = new InetSocketAddress("192.168.1.21", 40001);
    private static final long T0 = 1_000_000_000L;

    private record Sent(byte[] datagram, InetSocketAddress to) {
        Datagram.Header header() {
            return Datagram.decodeHeader(datagram, 0, datagram.length);
        }

        Message message() {
            return Codec.decodeFrame(FrameContext.HANDSHAKE, Direction.TO_CLIENT, datagram, Datagram.HEADER_SIZE,
                    datagram.length - Datagram.HEADER_SIZE);
        }
    }

    private static final class Recorder implements UdpHandshake.Sink {
        final List<Sent> sent = new ArrayList<>();
        final List<UdpHandshake.Request> requests = new ArrayList<>();

        @Override
        public void send(byte[] datagram, int length, InetSocketAddress to) {
            sent.add(new Sent(Arrays.copyOf(datagram, length), to));
        }

        @Override
        public void requested(UdpHandshake.Request request) {
            requests.add(request);
        }

        Sent last() {
            return sent.get(sent.size() - 1);
        }
    }

    private final Recorder recorder = new Recorder();
    private final UdpHandshake handshake = new UdpHandshake(TOKEN, new SecureRandom(), recorder);

    private static byte[] datagram(int channel, long connection, byte[] payload) {
        final byte[] out = new byte[Datagram.HEADER_SIZE + payload.length];
        Datagram.writeHeader(out, 0, channel, connection);
        System.arraycopy(payload, 0, out, Datagram.HEADER_SIZE, payload.length);
        return out;
    }

    private static byte[] knock() {
        return datagram(Datagram.HANDSHAKE_CHANNEL, 0, new byte[0]);
    }

    private static byte[] clientHello(byte[] proof) {
        final ClientHello hello = new ClientHello();
        hello.protocolVersion = 1;
        hello.clientVersion = "2.0.0";
        hello.proof = proof;
        return datagram(Datagram.HANDSHAKE_CHANNEL, 0, Codec.encodeFrame(hello));
    }

    private void receive(byte[] datagram, InetSocketAddress from, long nowUs) {
        handshake.receive(datagram, datagram.length, from, nowUs);
    }

    private byte[] knockFrom(InetSocketAddress from, long nowUs) {
        receive(knock(), from, nowUs);
        final Sent reply = recorder.last();
        assertEquals(from, reply.to());
        assertEquals(Datagram.HANDSHAKE_CHANNEL, reply.header().channel);
        assertEquals(0, reply.header().connection);
        return assertInstanceOf(ServerHello.class, reply.message()).nonce;
    }

    @Test
    void repeatedKnocksGetTheSameNonceUntilItExpires() {
        final byte[] first = knockFrom(CLIENT, T0);
        assertArrayEquals(first, knockFrom(CLIENT, T0 + 250_000));
        assertArrayEquals(first, knockFrom(CLIENT, T0 + UdpHandshake.NONCE_LIFETIME_US - 1));
        assertFalse(Arrays.equals(first, knockFrom(ROAMED, T0)));
        assertFalse(Arrays.equals(first, knockFrom(CLIENT, T0 + UdpHandshake.NONCE_LIFETIME_US)));
        assertTrue(recorder.requests.isEmpty());
    }

    @Test
    void aProvenClientHelloBindsTheClientOnceTheSessionAccepts() {
        final byte[] nonce = knockFrom(CLIENT, T0);
        receive(clientHello(Proofs.udpHello(TOKEN, nonce)), CLIENT, T0 + 1_000);
        assertEquals(1, recorder.requests.size());
        final UdpHandshake.Request request = recorder.requests.get(0);
        assertTrue(request.proven());
        assertEquals("2.0.0", request.hello().clientVersion);
        assertEquals(CLIENT, request.source());
        assertNull(handshake.bound());
        assertFalse(handshake.admits(CLIENT, 0));

        final int sentBefore = recorder.sent.size();
        receive(clientHello(Proofs.udpHello(TOKEN, nonce)), CLIENT, T0 + 250_000);
        assertEquals(1, recorder.requests.size());
        assertEquals(sentBefore, recorder.sent.size());

        final long connection = request.accept();
        assertNotEquals(0, connection);
        final Sent acceptance = recorder.last();
        assertEquals(CLIENT, acceptance.to());
        assertEquals(Datagram.HEADER_SIZE, acceptance.datagram().length);
        assertEquals(Datagram.HANDSHAKE_CHANNEL, acceptance.header().channel);
        assertEquals(connection, acceptance.header().connection);
        assertEquals(CLIENT, handshake.bound());
        assertEquals(connection, handshake.connection());
        assertTrue(handshake.admits(CLIENT, connection));
        assertFalse(handshake.admits(ROAMED, connection));
        assertFalse(handshake.admits(CLIENT, connection ^ 1));

        receive(clientHello(Proofs.udpHello(TOKEN, nonce)), CLIENT, T0 + 500_000);
        assertArrayEquals(acceptance.datagram(), recorder.last().datagram());
        assertEquals(1, recorder.requests.size());
        assertThrows(IllegalStateException.class, request::accept);
    }

    @Test
    void aWrongProofIsRejectedWithoutBindingAndTheRefusalRepeats() {
        final byte[] nonce = knockFrom(CLIENT, T0);
        final byte[] wrong = Proofs.hello(TOKEN, nonce);
        receive(clientHello(wrong), CLIENT, T0 + 1_000);
        final UdpHandshake.Request request = recorder.requests.get(0);
        assertFalse(request.proven());

        request.reject(ErrorCode.AUTHENTICATION_FAILED, "wrong session token");
        final Sent refusal = recorder.last();
        assertEquals(CLIENT, refusal.to());
        assertEquals(0, refusal.header().connection);
        final HandshakeError error = assertInstanceOf(HandshakeError.class, refusal.message());
        assertEquals(ErrorCode.AUTHENTICATION_FAILED, error.code);
        assertNull(handshake.bound());
        assertFalse(handshake.admits(CLIENT, 0));

        receive(clientHello(wrong), CLIENT, T0 + 250_000);
        assertArrayEquals(refusal.datagram(), recorder.last().datagram());
        assertEquals(1, recorder.requests.size());
        assertThrows(IllegalStateException.class, () -> request.reject(ErrorCode.AUTHENTICATION_FAILED, ""));
    }

    @Test
    void aClientHelloWithoutALiveNonceIsGreetedAfresh() {
        receive(clientHello(new byte[32]), CLIENT, T0);
        final ServerHello fresh = assertInstanceOf(ServerHello.class, recorder.last().message());
        assertTrue(recorder.requests.isEmpty());

        receive(clientHello(Proofs.udpHello(TOKEN, fresh.nonce)), CLIENT, T0 + 1_000);
        assertEquals(1, recorder.requests.size());
        assertTrue(recorder.requests.get(0).proven());
        receive(clientHello(Proofs.udpHello(TOKEN, fresh.nonce)), CLIENT, T0 + UdpHandshake.NONCE_LIFETIME_US);
        assertFalse(Arrays.equals(fresh.nonce, assertInstanceOf(ServerHello.class, recorder.last().message()).nonce));
        assertEquals(1, recorder.requests.size());
    }

    @Test
    void whatIsNotAWellFormedHandshakeDatagramIsIgnored() {
        final byte[] nonce = knockFrom(CLIENT, T0);
        final int sentBefore = recorder.sent.size();
        final byte[] good = clientHello(Proofs.udpHello(TOKEN, nonce));

        final byte[] badMagic = good.clone();
        badMagic[0] = 'g';
        receive(badMagic, CLIENT, T0 + 1);
        receive(datagram(0, 0, Arrays.copyOfRange(good, Datagram.HEADER_SIZE, good.length)), CLIENT, T0 + 2);
        final byte[] trailing = Arrays.copyOf(good, good.length + 1);
        receive(trailing, CLIENT, T0 + 3);
        final byte[] truncated = Arrays.copyOf(good, good.length - 1);
        receive(truncated, CLIENT, T0 + 4);
        final ChannelOpen open = new ChannelOpen();
        open.channel = ChannelId.VIDEO;
        open.proof = Proofs.channel(TOKEN, nonce, ChannelId.VIDEO.wireValue());
        receive(datagram(Datagram.HANDSHAKE_CHANNEL, 0, Codec.encodeFrame(open)), CLIENT, T0 + 5);

        assertEquals(sentBefore, recorder.sent.size());
        assertTrue(recorder.requests.isEmpty());
        receive(good, CLIENT, T0 + 6);
        assertEquals(1, recorder.requests.size());
    }

    @Test
    void aResumptionFromAnotherAddressMovesTheBinding() {
        final byte[] nonce = knockFrom(CLIENT, T0);
        receive(clientHello(Proofs.udpHello(TOKEN, nonce)), CLIENT, T0 + 1_000);
        final long first = recorder.requests.get(0).accept();

        final byte[] roamedNonce = knockFrom(ROAMED, T0 + 2_000_000);
        receive(clientHello(Proofs.udpHello(TOKEN, roamedNonce)), ROAMED, T0 + 2_001_000);
        final long second = recorder.requests.get(1).accept();

        assertNotEquals(first, second);
        assertEquals(ROAMED, handshake.bound());
        assertTrue(handshake.admits(ROAMED, second));
        assertFalse(handshake.admits(CLIENT, first));
        assertFalse(handshake.admits(CLIENT, second));
    }

    @Test
    void aFloodOfKnocksForgetsTheOldestNonceFirst() {
        final byte[] nonce = knockFrom(CLIENT, T0);
        for (int i = 0; i < UdpHandshake.MAX_SOURCES; ++i) {
            knockFrom(new InetSocketAddress("10.0.0." + (i + 1), 5000), T0 + 1 + i);
        }
        receive(clientHello(Proofs.udpHello(TOKEN, nonce)), CLIENT, T0 + 100);
        assertTrue(recorder.requests.isEmpty());
        assertFalse(Arrays.equals(nonce, assertInstanceOf(ServerHello.class, recorder.last().message()).nonce));
    }
}
