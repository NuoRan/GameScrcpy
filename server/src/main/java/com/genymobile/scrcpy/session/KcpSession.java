/*
 * KcpSession.java - WiFi 模式会话
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 */

package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.auxiliary.IAuxChannel;
import com.genymobile.scrcpy.auxiliary.NetTcpAuxChannel;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Streamer;
import com.genymobile.scrcpy.kcp.KcpControlChannel;
import com.genymobile.scrcpy.kcp.UdpVideoSender;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;
import java.io.InputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.Arrays;

/**
 * WiFi 模式会话 (UDP 视频 + KCP 控制 + TCP 音频/辅助)
 * <p>
 * 三通道架构:
 * <ul>
 *   <li>视频通道 (kcpPort)：裸 UDP 传输，无 ACK/重传开销，极致低延迟</li>
 *   <li>控制通道 (kcpPort+1)：KCP 可靠传输，确保触控/按键不丢失</li>
 *   <li>TCP 通道 (auxPort)：一个 TCP ServerSocket 接受两个连接:
 *       第一个连接用于音频流，第二个连接用于辅助通道</li>
 * </ul>
 */
public class KcpSession extends ScrcpySession {

    private static final byte[] AUDIO_HANDSHAKE = new byte[] { 'Q', 'S', 'A', 'U' };
    private static final byte[] AUX_HANDSHAKE = new byte[] { 'Q', 'S', 'A', 'X' };
    private static final int HANDSHAKE_READ_TIMEOUT_MS = 2000;

    private KcpControlChannel kcpControlChannel;
    private UdpVideoSender udpVideoSender;

    // TCP 音频/辅助共用一个 ServerSocket
    private ServerSocket tcpServerSocket;
    private Socket audioClientSocket;
    private Socket auxClientSocket;

    public KcpSession(Options options) {
        super(options);

        // 验证必需参数
        if (options.getClientIp().isEmpty()) {
            throw new IllegalArgumentException("KCP mode requires client_ip parameter");
        }
    }

    /**
     * 初始化 TCP ServerSocket（在 beforeRun 中调用，确保在创建 streamer 之前就绑好端口）
     */
    @Override
    protected void beforeRun() throws IOException {
        int tcpPort = options.getAuxPort();
        Ln.i("Starting TCP server on port " + tcpPort + " for audio + aux (WiFi mode)");

        tcpServerSocket = new ServerSocket(tcpPort);
        tcpServerSocket.setReuseAddress(true);
        tcpServerSocket.setSoTimeout(15000); // 15 秒超时

        // 通过显式握手分配角色，避免“连接先后顺序”导致 audio/aux 串线
        int expectedConnections = options.getAudio() ? 2 : 1;
        for (int i = 0; i < expectedConnections; i++) {
            Socket socket = tcpServerSocket.accept();
            socket.setTcpNoDelay(true);

            ChannelRole role = readChannelRole(socket);
            if (role == ChannelRole.AUDIO && audioClientSocket == null) {
                audioClientSocket = socket;
                Ln.i("Audio client connected from " + audioClientSocket.getRemoteSocketAddress() + " (handshake)");
                continue;
            }
            if (role == ChannelRole.AUX && auxClientSocket == null) {
                auxClientSocket = socket;
                Ln.i("Aux client connected from " + auxClientSocket.getRemoteSocketAddress() + " (handshake)");
                continue;
            }

            // 兼容旧客户端（无握手）或重复连接：按缺失角色回退分配
            if (options.getAudio() && audioClientSocket == null) {
                audioClientSocket = socket;
                Ln.w("Fallback assign accepted TCP socket as AUDIO (missing/invalid handshake)");
            } else if (auxClientSocket == null) {
                auxClientSocket = socket;
                Ln.w("Fallback assign accepted TCP socket as AUX (missing/invalid handshake)");
            } else {
                Ln.w("Extra TCP socket accepted, closing: " + socket.getRemoteSocketAddress());
                socket.close();
            }
        }

        if (options.getAudio() && audioClientSocket == null) {
            throw new IOException("Audio TCP connection was not established");
        }
        if (auxClientSocket == null) {
            throw new IOException("Aux TCP connection was not established");
        }
    }

    private enum ChannelRole {
        AUDIO,
        AUX,
        UNKNOWN
    }

    private ChannelRole readChannelRole(Socket socket) {
        try {
            int oldTimeout = socket.getSoTimeout();
            socket.setSoTimeout(HANDSHAKE_READ_TIMEOUT_MS);

            byte[] hs = new byte[4];
            InputStream is = socket.getInputStream();
            int read = 0;
            while (read < hs.length) {
                int n = is.read(hs, read, hs.length - read);
                if (n < 0) {
                    break;
                }
                read += n;
            }

            socket.setSoTimeout(oldTimeout);

            if (read == hs.length) {
                if (Arrays.equals(hs, AUDIO_HANDSHAKE)) {
                    return ChannelRole.AUDIO;
                }
                if (Arrays.equals(hs, AUX_HANDSHAKE)) {
                    return ChannelRole.AUX;
                }
            }
        } catch (SocketTimeoutException e) {
            Ln.w("TCP channel handshake timeout, fallback by role availability");
        } catch (IOException e) {
            Ln.w("Failed to read TCP channel handshake: " + e.getMessage());
        }
        return ChannelRole.UNKNOWN;
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
    protected Streamer createAudioStreamer() throws IOException {
        if (!options.getAudio() || audioClientSocket == null) {
            Ln.i("Audio disabled or no audio connection");
            return null;
        }

        return new Streamer(
                audioClientSocket.getOutputStream(),
                options.getAudioCodec(),
                options.getSendCodecMeta(),
                options.getSendFrameMeta()
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
    protected IAuxChannel createAuxChannel() throws IOException {
        if (auxClientSocket == null) {
            Ln.w("No aux TCP connection available");
            return null;
        }
        Ln.i("Creating TCP auxiliary channel from accepted connection");
        return new NetTcpAuxChannel(auxClientSocket);
    }

    @Override
    protected String getSessionName() {
        return String.format("WiFi mode (UDP video + KCP control + TCP audio/aux): video_port=%d, control_port=%d, tcp_port=%d, client=%s",
                options.getKcpPort(),
                options.getKcpControlPort(),
                options.getAuxPort(),
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
        if (audioClientSocket != null) {
            try {
                audioClientSocket.close();
            } catch (Exception e) {
                Ln.w("Error closing audio socket: " + e.getMessage());
            }
        }
        if (auxClientSocket != null) {
            try {
                auxClientSocket.close();
            } catch (Exception e) {
                Ln.w("Error closing aux socket: " + e.getMessage());
            }
        }
        if (tcpServerSocket != null) {
            try {
                tcpServerSocket.close();
            } catch (Exception e) {
                Ln.w("Error closing TCP server socket: " + e.getMessage());
            }
        }
    }
}
