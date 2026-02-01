package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.control.ControlChannel;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.device.ConfigurationException;
import com.genymobile.scrcpy.device.DesktopConnection;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Streamer;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;

/**
 * TcpSession - USB 模式会话 (TCP + adb forward)
 *
 * A-01 优化: 从 Server.scrcpyTcp() 提取的专用实现
 *
 * 特点:
 * - 使用 TCP 通过 adb forward 隧道传输
 * - 兼容性好, 适合 USB 连接
 * - 支持 tunnel_forward 模式
 */
public class TcpSession extends ScrcpySession {

    private DesktopConnection connection;

    public TcpSession(Options options) {
        super(options);
    }

    @Override
    protected IStreamer createVideoStreamer() throws IOException {
        Ln.i("Starting TCP video streaming");

        return new Streamer(
                connection.getVideoFd(),
                options.getVideoCodec(),
                options.getSendCodecMeta(),
                options.getSendFrameMeta()
        );
    }

    @Override
    protected IControlChannel createControlChannel() throws IOException {
        Ln.i("Starting TCP control channel");

        ControlChannel controlChannel = connection.getControlChannel();
        return controlChannel;
    }

    @Override
    protected String getSessionName() {
        return String.format("USB mode (TCP): tunnel_forward=%s",
                options.isTunnelForward());
    }

    /**
     * 在会话运行前建立连接
     */
    @Override
    protected void beforeRun() throws IOException, ConfigurationException {
        int scid = options.getScid();
        boolean tunnelForward = options.isTunnelForward();
        boolean control = options.getControl();
        boolean video = options.getVideo();
        boolean sendDummyByte = options.getSendDummyByte();

        connection = DesktopConnection.open(scid, tunnelForward, video, false, control, sendDummyByte);
    }

    @Override
    protected void onSessionInitialized() throws IOException {
        // 发送设备元数据
        if (options.getSendDeviceMeta()) {
            connection.sendDeviceMeta(Device.getDeviceName());
        }
    }

    @Override
    protected void onCleanup() {
        // 关闭连接
        if (connection != null) {
            try {
                connection.shutdown();
            } catch (IOException e) {
                Ln.w("Error shutting down connection: " + e.getMessage());
            }
            try {
                connection.close();
            } catch (IOException e) {
                Ln.w("Error closing connection: " + e.getMessage());
            }
        }
    }
}
