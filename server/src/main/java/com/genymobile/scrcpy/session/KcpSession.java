/*
 * KcpSession.java - WiFi 模式会话
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 */

package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.kcp.KcpControlChannel;
import com.genymobile.scrcpy.kcp.UdpVideoSender;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;

/**
 * WiFi 模式会话 (UDP 视频 + KCP 控制)
 * <p>
 * 特点:
 * <ul>
 *   <li>视频通道：裸 UDP 传输，无 ACK/重传开销，极致低延迟</li>
 *   <li>控制通道：KCP 可靠传输，确保触控/按键不丢失</li>
 *   <li>需要 client_ip 参数指定客户端地址</li>
 * </ul>
 */
public class KcpSession extends ScrcpySession {

    private KcpControlChannel kcpControlChannel;
    private UdpVideoSender udpVideoSender;

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

        Ln.i("Starting UDP video streaming to " + clientIp + ":" + port + " (pure UDP, no KCP)");

        udpVideoSender = new UdpVideoSender(
                clientIp,
                port,
                options.getVideoCodec(),
                options.getSendCodecMeta(),
                options.getSendFrameMeta(),
                options.getVideoBitRate()
        );
        return udpVideoSender;
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
        return String.format("WiFi mode (UDP video + KCP control): video_port=%d, control_port=%d, client=%s",
                options.getKcpPort(),
                options.getKcpControlPort(),
                options.getClientIp());
    }

    @Override
    protected void onCleanup() {
        if (udpVideoSender != null) {
            udpVideoSender.close();
            udpVideoSender = null;
        }
        if (kcpControlChannel != null) {
            try {
                kcpControlChannel.close();
            } catch (Exception e) {
                Ln.w("Error closing KCP control channel: " + e.getMessage());
            }
        }
    }
}
