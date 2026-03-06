package com.genymobile.scrcpy.auxiliary;

import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;

/**
 * UDP 模式辅助通道实现
 *
 * WiFi 模式下使用裸 UDP 接收 AuxMessage。
 * 消息极小 (≤10B) 且低频 (仅用户操作时)，不需要 KCP 可靠传输。
 */
public final class UdpAuxChannel implements IAuxChannel {

    private final DatagramSocket socket;
    private volatile Callback callback;
    private Thread thread;
    private volatile boolean running;

    /**
     * @param port 监听 UDP 端口
     */
    public UdpAuxChannel(int port) throws IOException {
        socket = new DatagramSocket(null);
        socket.setReuseAddress(true);
        socket.bind(new InetSocketAddress(port));
        Ln.i("UDP AuxChannel bound to port " + port);
    }

    @Override
    public void setCallback(Callback callback) {
        this.callback = callback;
    }

    @Override
    public void start() {
        running = true;
        thread = new Thread(() -> {
            Ln.i("UDP AuxChannel reader thread started");
            byte[] buf = new byte[64]; // 辅助消息很小，64B 足够
            DatagramPacket packet = new DatagramPacket(buf, buf.length);
            try {
                while (running) {
                    socket.receive(packet);
                    try {
                        AuxMessage msg = AuxMessageReader.parseFromBytes(
                                packet.getData(), packet.getOffset(), packet.getLength());
                        dispatch(msg);
                    } catch (IOException parseErr) {
                        Ln.w("UDP AuxChannel parse error: " + parseErr.getMessage());
                    }
                }
            } catch (IOException e) {
                if (running) {
                    Ln.w("UDP AuxChannel receive error: " + e.getMessage());
                }
            }
            Ln.i("UDP AuxChannel reader thread ended");
        }, "udp-aux-reader");
        thread.setDaemon(true);
        thread.start();
    }

    @Override
    public void stop() {
        running = false;
        if (socket != null && !socket.isClosed()) {
            socket.close(); // 打断 receive() 阻塞
        }
        if (thread != null) {
            thread.interrupt();
        }
    }

    @Override
    public void close() {
        stop();
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
