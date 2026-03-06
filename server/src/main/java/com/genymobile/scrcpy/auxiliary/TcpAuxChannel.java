package com.genymobile.scrcpy.auxiliary;

import android.net.LocalSocket;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;

/**
 * TCP 模式辅助通道实现
 *
 * 在独立线程中从 LocalSocket InputStream 循环读取 AuxMessage。
 */
public final class TcpAuxChannel implements IAuxChannel {

    private final LocalSocket socket;
    private final AuxMessageReader reader;
    private volatile Callback callback;
    private Thread thread;
    private volatile boolean running;

    public TcpAuxChannel(LocalSocket socket) throws IOException {
        this.socket = socket;
        this.reader = new AuxMessageReader(socket.getInputStream());
    }

    @Override
    public void setCallback(Callback callback) {
        this.callback = callback;
    }

    @Override
    public void start() {
        running = true;
        thread = new Thread(() -> {
            Ln.i("TCP AuxChannel reader thread started");
            try {
                while (running) {
                    AuxMessage msg = reader.recv();
                    dispatch(msg);
                }
            } catch (IOException e) {
                if (running) {
                    Ln.w("TCP AuxChannel read error: " + e.getMessage());
                }
            }
            Ln.i("TCP AuxChannel reader thread ended");
        }, "tcp-aux-reader");
        thread.setDaemon(true);
        thread.start();
    }

    @Override
    public void stop() {
        running = false;
        if (thread != null) {
            thread.interrupt();
        }
    }

    @Override
    public void close() throws IOException {
        stop();
        socket.close();
    }

    private void dispatch(AuxMessage msg) {
        Callback cb = callback;
        if (cb == null) return;

        switch (msg.getType()) {
            case AuxMessage.TYPE_SET_VIDEO_PARAMS:
                Ln.i("[AuxChannel] Video params: bitrate=" + msg.getVideoBitRate()
                        + " fps=" + msg.getVideoMaxFps() + " maxSize=" + msg.getVideoMaxSize());
                cb.onVideoParamsChanged(msg.getVideoBitRate(), msg.getVideoMaxFps(), msg.getVideoMaxSize());
                break;
            case AuxMessage.TYPE_SET_VIDEO_STREAMING:
                Ln.i("[AuxChannel] Video streaming: " + (msg.isVideoStreamingOn() ? "ON" : "OFF"));
                cb.onStreamingChanged(msg.isVideoStreamingOn());
                break;
        }
    }
}
