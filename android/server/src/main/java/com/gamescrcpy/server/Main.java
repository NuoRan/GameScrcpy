package com.gamescrcpy.server;

import android.os.Looper;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog;
import com.gamescrcpy.server.session.BuildDeviceDescriptor;
import com.gamescrcpy.server.session.SessionServer;
import com.gamescrcpy.server.transport.LocalSocketListener;
import com.gamescrcpy.server.transport.SessionTransport;
import com.gamescrcpy.server.transport.TransportGroup;
import com.gamescrcpy.server.transport.TunnelTransport;
import com.gamescrcpy.server.transport.UdpTransport;
import com.gamescrcpy.server.video.MediaCodecSessionFactory;
import com.gamescrcpy.server.video.NewDisplays;
import com.gamescrcpy.server.video.VideoController;
import com.gamescrcpy.server.wrappers.Displays;
import com.gamescrcpy.server.wrappers.PlatformException;
import com.gamescrcpy.server.wrappers.ProcessEnvironment;
import java.io.IOException;
import java.net.DatagramSocket;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class Main {

    public static final String SERVER_VERSION = "2.0.0";

    private Main() {
    }

    public static void main(String... args) {
        final Options options;
        final byte[] token;
        try {
            options = Options.parse(args);
            if (options.protocolVersion != Constants.PROTOCOL_VERSION) {
                fail(ErrorCode.VERSION_MISMATCH, "client protocol " + options.protocolVersion + ", server protocol "
                        + Constants.PROTOCOL_VERSION);
                return;
            }
            token = Options.readToken(System.in);
        } catch (IllegalArgumentException | IOException e) {
            fail(ErrorCode.INTERNAL_ERROR, e.getMessage());
            return;
        }
        Log.setMinimum(Log.parseLevel(options.logLevel));
        final Thread.UncaughtExceptionHandler runtimeHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            Log.e("uncaught exception on " + thread.getName(), error);
            if (runtimeHandler != null) {
                runtimeHandler.uncaughtException(thread, error);
            }
            System.exit(1);
        });
        ProcessEnvironment.prepare();

        final LocalSocketListener listener;
        try {
            listener = new LocalSocketListener(options.socketName);
        } catch (IOException e) {
            fail(ErrorCode.INTERNAL_ERROR, "cannot listen on the local socket: " + e.getMessage());
            return;
        }
        final SessionServer.Config config = new SessionServer.Config();
        config.lingerMillis = options.lingerMillis;
        final NewDisplays independent = newDisplays();
        final VideoController video = new VideoController(new MediaCodecSessionFactory(independent),
                EncoderCatalog::available, EncoderCatalog::supportsSize, Main::displaySnapshot, independent);
        final ServerSessionHandler handler = new ServerSessionHandler(video);
        Runtime.getRuntime().addShutdownHook(new Thread(handler::restoreDevice, "gs-restore"));
        final SecureRandom random = new SecureRandom();
        final TunnelTransport tunnel = new TunnelTransport(listener, token, TunnelTransport.HANDSHAKE_TIMEOUT_MILLIS,
                random);
        final SessionTransport transport;
        final String ready;
        if (options.udp) {
            final UdpTransport direct;
            try {
                direct = new UdpTransport(new DatagramSocket(options.udpPort), token, random,
                        () -> System.nanoTime() / 1000);
            } catch (SocketException e) {
                fail(ErrorCode.INTERNAL_ERROR, "cannot bind UDP port " + options.udpPort + ": " + e.getMessage());
                return;
            }
            transport = new TransportGroup(direct, tunnel);
            final String addresses = wirelessAddresses();
            ready = "GS_READY " + Constants.PROTOCOL_VERSION + " udp=" + direct.port()
                    + (addresses.isEmpty() ? "" : " ip=" + addresses);
        } else {
            transport = tunnel;
            ready = "GS_READY " + Constants.PROTOCOL_VERSION;
        }
        final SessionServer session = new SessionServer(transport,
                new BuildDeviceDescriptor(SERVER_VERSION, handler::capabilities), handler, config,
                () -> System.nanoTime() / 1000);
        final Thread sessionThread = new Thread(() -> {
            final SessionServer.Outcome outcome = session.run();
            handler.shutdown();
            Log.i("session ended (" + outcome + ")");
            System.exit(0);
        }, "gs-session");

        System.out.println(ready);
        System.out.flush();
        sessionThread.start();
        Looper.loop();
    }

    private static void fail(ErrorCode code, String message) {
        System.out.println("GS_FAILED " + code.wireValue() + " " + message);
        System.out.flush();
        System.exit(1);
    }

    private static DisplaySnapshot displaySnapshot(int displayId) {
        try {
            return Displays.get().snapshotOfAny(displayId);
        } catch (PlatformException e) {
            Log.w("cannot read display " + displayId, e);
            return null;
        }
    }

    private static NewDisplays newDisplays() {
        try {
            return NewDisplays.create();
        } catch (PlatformException e) {
            Log.i("no independent displays: " + e.getMessage());
            return null;
        }
    }

    static String wirelessAddresses() {
        final List<String> wireless = new ArrayList<>();
        final List<String> others = new ArrayList<>();
        try {
            for (NetworkInterface face : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!face.isUp() || face.isLoopback() || face.isVirtual()) {
                    continue;
                }
                final boolean wlan = face.getName().startsWith("wlan");
                for (InetAddress address : Collections.list(face.getInetAddresses())) {
                    if (address instanceof Inet4Address && !address.isLinkLocalAddress()) {
                        (wlan ? wireless : others).add(address.getHostAddress());
                    }
                }
            }
        } catch (SocketException | RuntimeException e) {
            Log.w("cannot list the network addresses", e);
        }
        wireless.addAll(others);
        return String.join(",", wireless);
    }
}
