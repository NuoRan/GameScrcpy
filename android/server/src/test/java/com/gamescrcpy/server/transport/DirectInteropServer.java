package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.server.Options;
import com.gamescrcpy.server.session.DeviceDescriptor;
import com.gamescrcpy.server.session.SessionServer;
import java.io.IOException;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.security.SecureRandom;

public final class DirectInteropServer {

    private DirectInteropServer() {
    }

    private static final class Plan {
        int frames = VIDEO_FRAMES;
        int frameBytes;
        int gapMillis = 2;
        boolean close;
        int audioPackets = AUDIO_PACKETS;
        int audioGapMillis = 5;
    }

    public static void main(String[] args) throws Exception {
        final Plan plan = new Plan();
        for (String arg : args) {
            final String[] option = arg.split("=", 2);
            final int value = Integer.parseInt(option[1]);
            switch (option[0]) {
                case "frames" -> plan.frames = value;
                case "frameBytes" -> plan.frameBytes = value;
                case "frameGapMillis" -> plan.gapMillis = value;
                case "closeVideoAfter" -> {
                    plan.frames = value;
                    plan.close = true;
                }
                case "audioPackets" -> plan.audioPackets = value;
                case "audioGapMillis" -> plan.audioGapMillis = value;
                default -> throw new IllegalArgumentException("unknown option " + arg);
            }
        }
        final byte[] token = Options.readToken(System.in);
        final UdpTransport transport = new UdpTransport(new DatagramSocket(0, InetAddress.getLoopbackAddress()), token,
                new SecureRandom(), () -> System.nanoTime() / 1000);
        final DeviceDescriptor device = () -> {
            final Welcome welcome = new Welcome();
            welcome.serverVersion = "interop";
            welcome.model = "Desktop JVM";
            welcome.sdkInt = 35;
            return welcome;
        };
        final SessionServer.Config config = new SessionServer.Config();
        config.lingerMillis = 0;
        final SessionServer server = new SessionServer(transport, device, new SessionServer.SessionHandler() {
            @Override
            public void handle(Message message, FrameEndpoint control) {
            }

            @Override
            public void connectionLost() {
            }

            @Override
            public void channelOpened(ChannelId channel, MediaEndpoint media) {
                final Thread stream = channel == ChannelId.VIDEO
                        ? new Thread(() -> streamVideo(media, plan), "interop-video")
                        : new Thread(() -> streamAudio(media, plan), "interop-audio");
                stream.setDaemon(true);
                stream.start();
            }
        }, config, () -> System.nanoTime() / 1000);
        System.out.println("GS_READY " + Constants.PROTOCOL_VERSION + " udp=" + transport.port());
        System.out.flush();
        final SessionServer.Outcome outcome = server.run();
        System.out.println("GS_ENDED " + outcome);
        System.out.flush();
    }

    public static final int VIDEO_FRAMES = 120;
    private static final int[] SIZES = {200, 5_000, 30_000, 800, 12_000};

    public static final int AUDIO_PACKETS = 50;

    private static void streamAudio(MediaEndpoint media, Plan plan) {
        try {
            for (int i = 0; i < plan.audioPackets; ++i) {
                final byte[] payload = new byte[60 + i % 40];
                for (int j = 0; j < payload.length; ++j) {
                    payload[j] = (byte) (i * 11 + j * 7);
                }
                final byte[] header = new byte[Constants.MEDIA_HEADER_SIZE];
                Media.writeHeader(header, 0, MediaChannel.AUDIO, MediaKind.FRAME, 0, 1, 20_000L * i, 0, payload.length);
                media.writePacket(header, ByteBuffer.wrap(payload));
                Thread.sleep(plan.audioGapMillis);
            }
        } catch (IOException | InterruptedException e) {
        }
    }

    private static void streamVideo(MediaEndpoint media, Plan plan) {
        try {
            for (int i = 0; i < plan.frames; ++i) {
                final byte[] payload = new byte[plan.frameBytes > 0 ? plan.frameBytes : SIZES[i % SIZES.length]];
                for (int j = 0; j < payload.length; ++j) {
                    payload[j] = (byte) (i * 7 + j * 13);
                }
                final byte[] header = new byte[Constants.MEDIA_HEADER_SIZE];
                Media.writeHeader(header, 0, MediaChannel.VIDEO, MediaKind.FRAME, i % 30 == 0 ? Media.FLAG_KEY_FRAME : 0,
                        1, 1000L * i, 0, payload.length);
                media.writePacket(header, ByteBuffer.wrap(payload));
                Thread.sleep(plan.gapMillis);
            }
            if (plan.close) {
                media.close();
            }
        } catch (IOException | InterruptedException e) {
        }
    }
}
