package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Datagram;
import com.gamescrcpy.protocol.DecodeError;
import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.HandshakeError;
import com.gamescrcpy.protocol.Messages.ServerHello;
import com.gamescrcpy.protocol.Proofs;
import java.net.InetSocketAddress;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

public final class UdpHandshake {

    public static final long NONCE_LIFETIME_US = 5_000_000L;
    static final int MAX_SOURCES = 16;

    public interface Sink {
        void send(byte[] datagram, int length, InetSocketAddress to);

        void requested(Request request);
    }

    private static final class Greeting {
        final byte[] nonce = new byte[16];
        final long createdUs;
        Request pending;
        byte[] answer;

        Greeting(long createdUs) {
            this.createdUs = createdUs;
        }
    }

    private final byte[] token;
    private final SecureRandom random;
    private final Sink sink;
    private final Map<InetSocketAddress, Greeting> greetings = new LinkedHashMap<>();
    private final byte[] out = new byte[Datagram.MAX_SIZE];
    private InetSocketAddress bound;
    private long connection;

    public UdpHandshake(byte[] token, SecureRandom random, Sink sink) {
        this.token = token.clone();
        this.random = random;
        this.sink = sink;
    }

    public void receive(byte[] datagram, int length, InetSocketAddress from, long nowUs) {
        final Request request;
        synchronized (this) {
            request = handle(datagram, length, from, nowUs);
        }
        if (request != null) {
            sink.requested(request);
        }
    }

    public synchronized boolean admits(InetSocketAddress from, long connection) {
        return bound != null && connection == this.connection && bound.equals(from);
    }

    public synchronized InetSocketAddress bound() {
        return bound;
    }

    public synchronized long connection() {
        return connection;
    }

    private Request handle(byte[] datagram, int length, InetSocketAddress from, long nowUs) {
        final Datagram.Header header;
        try {
            header = Datagram.decodeHeader(datagram, 0, length);
        } catch (DecodeException e) {
            return null;
        }
        if (header.channel != Datagram.HANDSHAKE_CHANNEL) {
            return null;
        }
        expire(nowUs);
        if (length == Datagram.HEADER_SIZE) {
            greet(from, nowUs);
            return null;
        }
        final ClientHello hello;
        try {
            hello = clientHello(datagram, length);
        } catch (DecodeException e) {
            return null;
        }
        if (hello == null) {
            return null;
        }
        final Greeting greeting = greetings.get(from);
        if (greeting == null) {
            greet(from, nowUs);
            return null;
        }
        if (greeting.answer != null) {
            sink.send(greeting.answer, greeting.answer.length, from);
            return null;
        }
        if (greeting.pending != null) {
            return null;
        }
        greeting.pending = new Request(hello, Proofs.matches(Proofs.udpHello(token, greeting.nonce), hello.proof),
                from, greeting);
        return greeting.pending;
    }

    private void greet(InetSocketAddress from, long nowUs) {
        Greeting greeting = greetings.get(from);
        if (greeting == null) {
            if (greetings.size() == MAX_SOURCES) {
                final Iterator<Greeting> oldest = greetings.values().iterator();
                oldest.next();
                oldest.remove();
            }
            greeting = new Greeting(nowUs);
            random.nextBytes(greeting.nonce);
            greetings.put(from, greeting);
        }
        final ServerHello hello = new ServerHello();
        hello.protocolVersion = Constants.PROTOCOL_VERSION;
        hello.nonce = greeting.nonce;
        sink.send(out, encode(hello), from);
    }

    private void expire(long nowUs) {
        greetings.values().removeIf(greeting -> nowUs - greeting.createdUs >= NONCE_LIFETIME_US);
    }

    private static ClientHello clientHello(byte[] datagram, int length) {
        final int size = length - Datagram.HEADER_SIZE;
        final Codec.FrameHeader frame = Codec.decodeFrameHeader(FrameContext.HANDSHAKE, Direction.TO_SERVER, datagram,
                Datagram.HEADER_SIZE, size);
        if (size > Constants.FRAME_HEADER_SIZE + frame.length) {
            throw new DecodeException(DecodeError.TRAILING_BYTES);
        }
        final Message message = Codec.decodeFrame(FrameContext.HANDSHAKE, Direction.TO_SERVER, datagram,
                Datagram.HEADER_SIZE, size);
        return message instanceof ClientHello hello ? hello : null;
    }

    private int encode(Message message) {
        Datagram.writeHeader(out, 0, Datagram.HANDSHAKE_CHANNEL, 0);
        return Datagram.HEADER_SIZE + Codec.encodeFrameInto(message, out, Datagram.HEADER_SIZE);
    }

    public final class Request {
        private final ClientHello hello;
        private final boolean proven;
        private final InetSocketAddress source;
        private final Greeting greeting;
        private boolean done;

        Request(ClientHello hello, boolean proven, InetSocketAddress source, Greeting greeting) {
            this.hello = hello;
            this.proven = proven;
            this.source = source;
            this.greeting = greeting;
        }

        public ClientHello hello() {
            return hello;
        }

        public boolean proven() {
            return proven;
        }

        public InetSocketAddress source() {
            return source;
        }

        public long accept() {
            synchronized (UdpHandshake.this) {
                answer();
                long number;
                do {
                    number = random.nextInt() & 0xFFFF_FFFFL;
                } while (number == 0 || number == connection);
                connection = number;
                bound = source;
                Datagram.writeHeader(out, 0, Datagram.HANDSHAKE_CHANNEL, number);
                remember(Datagram.HEADER_SIZE);
                return number;
            }
        }

        public void reject(ErrorCode code, String message) {
            synchronized (UdpHandshake.this) {
                answer();
                final HandshakeError error = new HandshakeError();
                error.code = code;
                error.message = message;
                remember(encode(error));
            }
        }

        private void answer() {
            if (done) {
                throw new IllegalStateException("the session request was answered already");
            }
            done = true;
            greeting.pending = null;
        }

        private void remember(int length) {
            greeting.answer = Arrays.copyOf(out, length);
            sink.send(greeting.answer, length, source);
        }
    }
}
