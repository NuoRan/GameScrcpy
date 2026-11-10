package com.gamescrcpy.server;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

public final class Options {

    public final int protocolVersion;
    public final String socketName;
    public final boolean udp;
    public final int udpPort;
    public final long lingerMillis;
    public final String logLevel;

    private Options(int protocolVersion, String socketName, boolean udp, int udpPort, long lingerMillis,
                    String logLevel) {
        this.protocolVersion = protocolVersion;
        this.socketName = socketName;
        this.udp = udp;
        this.udpPort = udpPort;
        this.lingerMillis = lingerMillis;
        this.logLevel = logLevel;
    }

    public static Options parse(String... args) {
        if (args.length == 0) {
            throw new IllegalArgumentException("missing protocol version");
        }
        final int version = parseInteger("protocol version", args[0], 1, 0xFFFF);
        String socket = null;
        boolean udp = false;
        int udpPort = 0;
        long linger = 15_000;
        String log = "info";
        for (int i = 1; i < args.length; ++i) {
            final int equals = args[i].indexOf('=');
            if (equals <= 0) {
                throw new IllegalArgumentException("malformed argument: " + args[i]);
            }
            final String key = args[i].substring(0, equals);
            final String value = args[i].substring(equals + 1);
            switch (key) {
                case "socket" -> {
                    if (!value.matches("[A-Za-z0-9_]{1,64}")) {
                        throw new IllegalArgumentException("malformed socket name: " + value);
                    }
                    socket = value;
                }
                case "transport" -> {
                    if (!value.matches("adb|udp")) {
                        throw new IllegalArgumentException("unsupported transport: " + value);
                    }
                    udp = value.equals("udp");
                }
                case "udpPort" -> udpPort = parseInteger("udpPort", value, 0, 65_535);
                case "lingerMs" -> linger = parseInteger("lingerMs", value, 0, 600_000);
                case "log" -> {
                    if (!value.matches("trace|debug|info|warning|error")) {
                        throw new IllegalArgumentException("unknown log level: " + value);
                    }
                    log = value;
                }
                default -> throw new IllegalArgumentException("unknown argument: " + key);
            }
        }
        if (socket == null) {
            throw new IllegalArgumentException("missing socket=<name>");
        }
        return new Options(version, socket, udp, udpPort, linger, log);
    }

    public static byte[] readToken(InputStream input) throws IOException {
        final BufferedReader reader = new BufferedReader(new InputStreamReader(input, StandardCharsets.US_ASCII));
        final String line = reader.readLine();
        if (line == null || !line.matches("GS_TOKEN=[0-9a-f]{32}")) {
            throw new IllegalArgumentException("standard input must start with GS_TOKEN=<32 lowercase hex digits>");
        }
        final byte[] token = new byte[16];
        for (int i = 0; i < token.length; ++i) {
            token[i] = (byte) Integer.parseInt(line.substring(9 + 2 * i, 11 + 2 * i), 16);
        }
        return token;
    }

    private static int parseInteger(String what, String text, int min, int max) {
        try {
            final int value = Integer.parseInt(text);
            if (value < min || value > max) {
                throw new IllegalArgumentException(what + " out of range: " + text);
            }
            return value;
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("malformed " + what + ": " + text, e);
        }
    }
}
