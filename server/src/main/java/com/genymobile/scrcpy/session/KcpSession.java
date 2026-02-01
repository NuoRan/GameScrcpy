package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.kcp.KcpControlChannel;
import com.genymobile.scrcpy.kcp.KcpVideoSender;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;

/**
 * KcpSession - WiFi 模式会话 (KCP/UDP)
 *
 * A-01 优化: 从 Server.scrcpyKcp() 提取的专用实现
 *
 * 特点:
 * - 使用 KCP 协议实现可靠 UDP 传输
 * - 低延迟, 适合 WiFi 环境
 * - 需要 client_ip 参数
 */
public class KcpSession extends ScrcpySession {

    private KcpControlChannel kcpControlChannel;

    public KcpSession(Options options) {
        super(options);

        // 验证必需参数
        if (options.getClientIp().isEmpty()) {
            throw new IllegalArgumentException("KCP mode requires client_ip parameter");
        }
    }

    @Override
    protected IStreamer createVideoStreamer() throws IOException {
        String clientIp = options.getClientIp();
        int port = options.getKcpPort();

        Ln.i("Starting KCP video streaming to " + clientIp + ":" + port);

        return new KcpVideoSender(
                clientIp,
                port,
                options.getVideoCodec(),
                options.getSendCodecMeta(),
                options.getSendFrameMeta(),
                options.getVideoBitRate()
        );
    }

    @Override
    protected IControlChannel createControlChannel() throws IOException {
        String clientIp = options.getClientIp();
        int port = options.getKcpControlPort();

        Ln.i("Starting KCP control channel to " + clientIp + ":" + port);

        kcpControlChannel = new KcpControlChannel(clientIp, port);
        return kcpControlChannel;
    }

    @Override
    protected String getSessionName() {
        return String.format("WiFi mode (KCP): video_port=%d, control_port=%d, client=%s",
                options.getKcpPort(),
                options.getKcpControlPort(),
                options.getClientIp());
    }

    @Override
    protected void onCleanup() {
        if (kcpControlChannel != null) {
            try {
                kcpControlChannel.close();
            } catch (Exception e) {
                Ln.w("Error closing KCP control channel: " + e.getMessage());
            }
        }
    }
}
