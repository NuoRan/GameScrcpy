package com.gamescrcpy.server;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

class OptionsTest {

    @Test
    void parsesTheLaunchArguments() {
        final Options options = Options.parse("1", "socket=gamescrcpy_0123456789abcdef", "transport=adb",
                "lingerMs=5000", "log=debug");
        assertEquals(1, options.protocolVersion);
        assertEquals("gamescrcpy_0123456789abcdef", options.socketName);
        assertEquals(5000, options.lingerMillis);
        assertEquals("debug", options.logLevel);
        assertFalse(options.udp);
        assertEquals(15_000, Options.parse("1", "socket=s").lingerMillis);

        final Options direct = Options.parse("1", "socket=s", "transport=udp", "udpPort=27183");
        assertTrue(direct.udp);
        assertEquals(27183, direct.udpPort);
        assertEquals(0, Options.parse("1", "socket=s", "transport=udp").udpPort);
    }

    @Test
    void rejectsMissingUnknownAndMalformedArguments() {
        assertThrows(IllegalArgumentException.class, Options::parse);
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("x", "socket=s"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket=s", "colour=red"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket=bad name"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket=s", "transport=quic"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket=s", "udpPort=65536"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket=s", "lingerMs=-1"));
        assertThrows(IllegalArgumentException.class, () -> Options.parse("1", "socket"));
    }

    @Test
    void readsTheTokenFromTheFirstLineOfStandardInput() throws Exception {
        final byte[] token = Options.readToken(new ByteArrayInputStream(
                "GS_TOKEN=000102030405060708090a0b0c0d0eff\nrest".getBytes(StandardCharsets.US_ASCII)));
        assertArrayEquals(new byte[] {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, (byte) 0xFF}, token);
        assertThrows(IllegalArgumentException.class, () -> Options.readToken(
                new ByteArrayInputStream("GS_TOKEN=ABC\n".getBytes(StandardCharsets.US_ASCII))));
        assertThrows(IllegalArgumentException.class, () -> Options.readToken(new ByteArrayInputStream(new byte[0])));
    }
}
