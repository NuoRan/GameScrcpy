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
import com.genymobile.scrcpy.kcp.KcpVideoSender;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;

/**
 * WiFi 模式会话 (KCP/UDP)
 * <p>
 * 特点:
 * <ul>
 *   <li>使用 KCP 协议实现可靠 UDP 传输</li>
 *   <li>低延迟，适合 WiFi 环境</li>
 *   <li>需要 client_ip 参数指定客户端地址</li>
 * </ul>
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
