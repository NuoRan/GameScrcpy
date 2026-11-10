package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ChannelAccepted;
import com.gamescrcpy.protocol.Messages.ChannelOpen;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.HandshakeError;
import com.gamescrcpy.protocol.Messages.ServerHello;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.protocol.Proofs;
import java.io.IOException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class TunnelTransport implements SessionTransport {

    public static final int HANDSHAKE_TIMEOUT_MILLIS = 5_000;

    private final Listener listener;
    private final byte[] token;
    private final int handshakeTimeoutMillis;
    private final SecureRandom random;
    private final Set<StreamEndpoint> pending = new HashSet<>();
    private Events events;
    private Thread acceptor;
    private volatile boolean closed;

    public TunnelTransport(Listener listener, byte[] token, int handshakeTimeoutMillis, SecureRandom random) {
        this.listener = listener;
        this.token = token.clone();
        this.handshakeTimeoutMillis = handshakeTimeoutMillis;
        this.random = random;
    }

    @Override
    public void start(Events events) {
        if (acceptor != null) {
            throw new IllegalStateException("started twice");
        }
        this.events = events;
        acceptor = new Thread(this::acceptLoop, "gs-accept");
        acceptor.setDaemon(true);
        acceptor.start();
    }

    @Override
    public void close() throws IOException {
        closed = true;
        final List<StreamEndpoint> unanswered;
        synchronized (this) {
            unanswered = new ArrayList<>(pending);
            pending.clear();
        }
        for (StreamEndpoint stream : unanswered) {
            stream.close();
        }
        listener.close();
    }

    private void acceptLoop() {
        while (!closed) {
            final Connection connection;
            try {
                connection = listener.accept();
            } catch (IOException e) {
                return;
            }
            StreamEndpoint stream = null;
            try {
                stream = new StreamEndpoint(connection);
                greet(stream);
            } catch (IOException | DecodeException e) {
                if (stream != null) {
                    stream.close();
                } else {
                    closeQuietly(connection);
                }
            }
        }
    }

    private void greet(StreamEndpoint stream) throws IOException {
        stream.setReadTimeout(handshakeTimeoutMillis);
        final byte[] nonce = new byte[16];
        random.nextBytes(nonce);
        final ServerHello hello = new ServerHello();
        hello.protocolVersion = Constants.PROTOCOL_VERSION;
        hello.nonce = nonce;
        stream.write(hello);

        final Message first = stream.read(FrameContext.HANDSHAKE);
        if (first instanceof ClientHello clientHello) {
            if (track(stream)) {
                events.sessionRequested(new TunnelSessionRequest(stream, clientHello, nonce));
            }
        } else if (first instanceof ChannelOpen open) {
            if (!Proofs.matches(Proofs.channel(token, nonce, open.channel.wireValue()), open.proof)) {
                reject(stream, ErrorCode.AUTHENTICATION_FAILED, "wrong session token");
            } else if (track(stream)) {
                events.channelRequested(new TunnelChannelRequest(stream, open.channel));
            }
        } else {
            reject(stream, ErrorCode.UNEXPECTED_MESSAGE, "expected ClientHello or ChannelOpen");
        }
    }

    private boolean track(StreamEndpoint stream) {
        synchronized (this) {
            if (!closed) {
                pending.add(stream);
                return true;
            }
        }
        stream.close();
        return false;
    }

    private synchronized void answered(StreamEndpoint stream) {
        pending.remove(stream);
    }

    private static void reject(StreamEndpoint stream, ErrorCode code, String message) {
        final HandshakeError error = new HandshakeError();
        error.code = code;
        error.message = message;
        try {
            stream.write(error);
        } catch (IOException ignored) {
        }
        stream.close();
    }

    private static void closeQuietly(Connection connection) {
        try {
            connection.close();
        } catch (IOException ignored) {
        }
    }

    private final class TunnelSessionRequest implements SessionRequest {
        private final StreamEndpoint stream;
        private final ClientHello hello;
        private final byte[] nonce;
        private boolean done;

        TunnelSessionRequest(StreamEndpoint stream, ClientHello hello, byte[] nonce) {
            this.stream = stream;
            this.hello = hello;
            this.nonce = nonce;
        }

        @Override
        public ClientHello hello() {
            return hello;
        }

        @Override
        public boolean proven() {
            return Proofs.matches(Proofs.hello(token, nonce), hello.proof);
        }

        @Override
        public FrameEndpoint accept(Welcome welcome) throws IOException {
            answer();
            try {
                stream.write(welcome);
            } catch (IOException e) {
                stream.close();
                throw e;
            }
            return stream;
        }

        @Override
        public void reject(ErrorCode code, String message) {
            answer();
            TunnelTransport.reject(stream, code, message);
        }

        private void answer() {
            if (done) {
                throw new IllegalStateException("the session request was answered already");
            }
            done = true;
            answered(stream);
        }
    }

    private final class TunnelChannelRequest implements ChannelRequest {
        private final StreamEndpoint stream;
        private final ChannelId channel;
        private boolean done;

        TunnelChannelRequest(StreamEndpoint stream, ChannelId channel) {
            this.stream = stream;
            this.channel = channel;
        }

        @Override
        public ChannelId channel() {
            return channel;
        }

        @Override
        public MediaEndpoint acceptMedia() throws IOException {
            return accepted();
        }

        @Override
        public FrameEndpoint acceptFrames() throws IOException {
            return accepted();
        }

        @Override
        public void reject(ErrorCode code, String message) {
            answer();
            TunnelTransport.reject(stream, code, message);
        }

        private StreamEndpoint accepted() throws IOException {
            answer();
            final ChannelAccepted accepted = new ChannelAccepted();
            accepted.channel = channel;
            try {
                stream.write(accepted);
                stream.setReadTimeout(0);
            } catch (IOException e) {
                stream.close();
                throw e;
            }
            return stream;
        }

        private void answer() {
            if (done) {
                throw new IllegalStateException("the channel request was answered already");
            }
            done = true;
            answered(stream);
        }
    }
}
