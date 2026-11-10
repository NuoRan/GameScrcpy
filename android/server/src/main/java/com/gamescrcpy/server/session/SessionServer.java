package com.gamescrcpy.server.session;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.Goodbye;
import com.gamescrcpy.protocol.Messages.Ping;
import com.gamescrcpy.protocol.Messages.Pong;
import com.gamescrcpy.protocol.Messages.ServerGoodbye;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.server.transport.ChannelRequest;
import com.gamescrcpy.server.transport.FrameEndpoint;
import com.gamescrcpy.server.transport.MediaEndpoint;
import com.gamescrcpy.server.transport.SessionRequest;
import com.gamescrcpy.server.transport.SessionTransport;
import java.io.Closeable;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.EnumMap;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

public final class SessionServer implements SessionTransport.Events {

    public static final class Config {
        public int protocolVersion = Constants.PROTOCOL_VERSION;
        public long firstConnectionTimeoutMillis = 10_000;
        public int heartbeatTimeoutMillis = 6_000;
        public long lingerMillis = 15_000;
    }

    public interface SessionHandler {
        default void controlConnected(FrameEndpoint control) {
        }

        default void channelOpened(ChannelId channel, MediaEndpoint media) {
        }

        default void bulkOpened(FrameEndpoint bulk) {
        }

        void handle(Message message, FrameEndpoint control) throws IOException;

        void connectionLost();
    }

    public enum Outcome {
        GOODBYE,
        LINGER_EXPIRED,
        NO_CLIENT,
        PROTOCOL_ERROR,
    }

    private enum State { WAITING, ACTIVE, LINGERING, ENDED }

    private final SessionTransport transport;
    private final DeviceDescriptor device;
    private final SessionHandler handler;
    private final Config config;
    private final LongSupplier monotonicMicros;

    private final BlockingQueue<SessionRequest> requests = new LinkedBlockingQueue<>();
    private final Map<ChannelId, Closeable> channels = new EnumMap<>(ChannelId.class);
    private volatile State state = State.WAITING;
    private volatile FrameEndpoint active;

    public SessionServer(SessionTransport transport, DeviceDescriptor device, SessionHandler handler, Config config,
                         LongSupplier monotonicMicros) {
        this.transport = transport;
        this.device = device;
        this.handler = handler;
        this.config = config;
        this.monotonicMicros = monotonicMicros;
    }

    public Outcome run() {
        transport.start(this);
        try {
            long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(config.firstConnectionTimeoutMillis);
            boolean resuming = false;
            while (true) {
                final SessionRequest request = nextRequest(deadline);
                if (request == null) {
                    return resuming ? Outcome.LINGER_EXPIRED : Outcome.NO_CLIENT;
                }
                final FrameEndpoint control = establish(request, resuming);
                if (control == null) {
                    continue;
                }
                final Outcome ended = requests.isEmpty() ? serve(control) : takenOver(control);
                if (ended != null) {
                    return ended;
                }
                handler.connectionLost();
                closeChannels();
                state = State.LINGERING;
                resuming = true;
                deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(Math.max(0, config.lingerMillis));
            }
        } finally {
            state = State.ENDED;
            try {
                transport.close();
            } catch (IOException ignored) {
            }
            closeChannels();
        }
    }

    @Override
    public void sessionRequested(SessionRequest request) {
        if (state == State.ACTIVE) {
            if (request.hello().protocolVersion != config.protocolVersion || !request.proven()) {
                request.reject(ErrorCode.UNEXPECTED_MESSAGE, "a session is already established");
                return;
            }
            requests.add(request);
            final FrameEndpoint current = active;
            if (current != null) {
                closeQuietly(current);
            }
            return;
        }
        requests.add(request);
    }

    @Override
    public void channelRequested(ChannelRequest request) {
        if (state != State.ACTIVE) {
            request.reject(ErrorCode.UNEXPECTED_MESSAGE, "no session is established");
            return;
        }
        final ChannelId channel = request.channel();
        final Closeable replaced;
        try {
            if (channel == ChannelId.BULK) {
                final FrameEndpoint bulk = request.acceptFrames();
                replaced = remember(channel, bulk);
                handler.bulkOpened(bulk);
            } else {
                final MediaEndpoint media = request.acceptMedia();
                replaced = remember(channel, media);
                handler.channelOpened(channel, media);
            }
        } catch (IOException e) {
            return;
        }
        if (replaced != null) {
            closeQuietly(replaced);
        }
    }

    private Closeable remember(ChannelId channel, Closeable endpoint) {
        synchronized (channels) {
            return channels.put(channel, endpoint);
        }
    }

    private SessionRequest nextRequest(long deadlineNanos) {
        final long remaining = deadlineNanos - System.nanoTime();
        if (remaining <= 0) {
            return requests.poll();
        }
        try {
            return requests.poll(remaining, TimeUnit.NANOSECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        }
    }

    private FrameEndpoint establish(SessionRequest request, boolean resuming) {
        final ClientHello hello = request.hello();
        if (hello.protocolVersion != config.protocolVersion) {
            request.reject(ErrorCode.VERSION_MISMATCH, "client protocol " + hello.protocolVersion
                    + ", server protocol " + config.protocolVersion + "; reinstall the matching version");
            return null;
        }
        if (!request.proven()) {
            request.reject(ErrorCode.AUTHENTICATION_FAILED, "wrong session token");
            return null;
        }
        final Welcome welcome = device.describe();
        welcome.resumed = resuming;
        final State before = state;
        state = State.ACTIVE;
        final FrameEndpoint control;
        try {
            control = request.accept(welcome);
        } catch (IOException e) {
            state = before;
            closeChannels();
            return null;
        }
        active = control;
        handler.controlConnected(control);
        final FrameEndpoint bulk = request.acceptedBulk();
        if (bulk != null) {
            handler.bulkOpened(bulk);
        }
        return control;
    }

    private Outcome serve(FrameEndpoint control) {
        try {
            control.setReadTimeout(config.heartbeatTimeoutMillis);
            while (true) {
                final Message message = control.read(FrameContext.CONTROL);
                if (message instanceof Ping ping) {
                    final Pong pong = new Pong();
                    pong.nonce = ping.nonce;
                    pong.clientTimeUs = ping.clientTimeUs;
                    pong.serverTimeUs = monotonicMicros.getAsLong();
                    control.write(pong);
                } else if (message instanceof Goodbye) {
                    return Outcome.GOODBYE;
                } else {
                    handler.handle(message, control);
                }
            }
        } catch (DecodeException e) {
            final ServerGoodbye goodbye = new ServerGoodbye();
            goodbye.reason = errorCodeFor(e);
            goodbye.message = "malformed control frame: " + e.error().specName();
            try {
                control.write(goodbye);
            } catch (IOException ignored) {
            }
            handler.connectionLost();
            return Outcome.PROTOCOL_ERROR;
        } catch (SocketTimeoutException e) {
            return null;
        } catch (IOException e) {
            return null;
        } finally {
            active = null;
            control.close();
        }
    }

    private Outcome takenOver(FrameEndpoint control) {
        active = null;
        control.close();
        return null;
    }

    private void closeChannels() {
        synchronized (channels) {
            for (Closeable channel : channels.values()) {
                closeQuietly(channel);
            }
            channels.clear();
        }
    }

    private static ErrorCode errorCodeFor(DecodeException e) {
        return switch (e.error()) {
            case FRAME_TOO_LARGE -> ErrorCode.FRAME_TOO_LARGE;
            case UNKNOWN_TYPE -> ErrorCode.UNKNOWN_TYPE;
            case WRONG_DIRECTION -> ErrorCode.WRONG_DIRECTION;
            case UNEXPECTED_TYPE -> ErrorCode.UNEXPECTED_MESSAGE;
            default -> ErrorCode.MALFORMED_FRAME;
        };
    }

    private static void closeQuietly(Closeable endpoint) {
        try {
            endpoint.close();
        } catch (IOException ignored) {
        }
    }
}
