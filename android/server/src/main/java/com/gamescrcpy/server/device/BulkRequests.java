package com.gamescrcpy.server.device;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.RequestFailed;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.transport.FrameEndpoint;
import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

public final class BulkRequests implements AutoCloseable {

    public static final int MAX_OUTSTANDING = 4;

    private static final int MESSAGE_LIMIT = 1000;

    public static class Failure extends Exception {
        private static final long serialVersionUID = 1L;

        private final ErrorCode code;

        public Failure(ErrorCode code, String message) {
            super(message);
            this.code = code;
        }

        public Failure(ErrorCode code, String message, Throwable cause) {
            super(message, cause);
            this.code = code;
        }

        public ErrorCode code() {
            return code;
        }
    }

    public interface Reply {
        Message make() throws Failure;
    }

    private final ExecutorService replies = Executors.newSingleThreadExecutor(runnable -> {
        final Thread thread = new Thread(runnable, "gs-bulk-out");
        thread.setDaemon(true);
        return thread;
    });
    private final Object lock = new Object();
    private FrameEndpoint bulk;
    private int outstanding;

    public void bulkOpened(FrameEndpoint endpoint) {
        synchronized (lock) {
            bulk = endpoint;
        }
    }

    public void connectionLost() {
        synchronized (lock) {
            bulk = null;
        }
    }

    public void submit(long requestId, Executor executor, Reply reply) {
        final FrameEndpoint target;
        final boolean accepted;
        synchronized (lock) {
            target = bulk;
            accepted = target != null && outstanding < MAX_OUTSTANDING;
            if (accepted) {
                ++outstanding;
            }
        }
        if (target == null) {
            Log.d("bulk request " + requestId + " dropped: no bulk channel");
            return;
        }
        if (!accepted) {
            send(target, failure(requestId, ErrorCode.RESOURCE_BUSY, "more than " + MAX_OUTSTANDING
                    + " bulk requests at once"), false);
            return;
        }
        try {
            executor.execute(() -> answer(requestId, reply));
        } catch (RejectedExecutionException e) {
            finished();
            Log.d("bulk request " + requestId + " dropped: the server is closing");
        }
    }

    @Override
    public void close() {
        replies.shutdownNow();
    }

    private void answer(long requestId, Reply reply) {
        Message message;
        try {
            message = reply.make();
        } catch (Failure e) {
            message = failure(requestId, e.code(), e.getMessage());
        } catch (RuntimeException e) {
            message = failure(requestId, ErrorCode.INTERNAL_ERROR, String.valueOf(e));
        }
        final FrameEndpoint target;
        synchronized (lock) {
            target = bulk;
        }
        if (target == null) {
            Log.d("bulk reply " + requestId + " dropped: no bulk channel");
            finished();
            return;
        }
        send(target, message, true);
    }

    private void send(FrameEndpoint target, Message message, boolean counted) {
        try {
            replies.execute(() -> {
                try {
                    target.write(message);
                } catch (IOException e) {
                    Log.d("bulk reply not sent: " + e.getMessage());
                } catch (RuntimeException e) {
                    Log.w("bulk reply not sent", e);
                } finally {
                    if (counted) {
                        finished();
                    }
                }
            });
        } catch (RejectedExecutionException e) {
            if (counted) {
                finished();
            }
            Log.d("bulk reply not sent: the server is closing");
        }
    }

    private void finished() {
        synchronized (lock) {
            --outstanding;
        }
    }

    private static RequestFailed failure(long requestId, ErrorCode code, String message) {
        final RequestFailed failed = new RequestFailed();
        failed.requestId = requestId;
        failed.code = code;
        failed.message = message == null ? "" : message.length() > MESSAGE_LIMIT ? message.substring(0, MESSAGE_LIMIT)
                : message;
        return failed;
    }
}
