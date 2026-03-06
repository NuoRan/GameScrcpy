package com.genymobile.scrcpy.auxiliary;

import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;
import java.net.Socket;

/**
 * 基于 java.net.Socket 的 TCP 辅助通道实现
 *
 * 用于 WiFi/KCP 模式，从网络 TCP Socket 读取 AuxMessage。
 * 与 TcpAuxChannel (使用 LocalSocket) 的区别是使用标准网络 Socket。
 */
public final class NetTcpAuxChannel implements IAuxChannel {

    private final Socket socket;
    private final AuxMessageReader reader;
    private volatile Callback callback;
    private Thread thread;
    private volatile boolean running;

    public NetTcpAuxChannel(Socket socket) throws IOException {
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
            Ln.i("NetTCP AuxChannel reader thread started");
            try {
                while (running) {
                    AuxMessage msg = reader.recv();
                    dispatch(msg);
                }
            } catch (IOException e) {
                if (running) {
                    Ln.w("NetTCP AuxChannel read error: " + e.getMessage());
                }
            }
            Ln.i("NetTCP AuxChannel reader thread ended");
        }, "net-tcp-aux-reader");
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
