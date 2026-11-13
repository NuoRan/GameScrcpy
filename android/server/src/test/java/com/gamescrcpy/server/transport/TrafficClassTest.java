package com.gamescrcpy.server.transport;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.util.Arrays;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

class TrafficClassTest {

    private static final InetAddress LOOPBACK = InetAddress.getLoopbackAddress();

    private static final class RecordingSocket extends DatagramSocket {
        int requested = -1;
        boolean accepted;

        RecordingSocket() throws SocketException {
            super(0, LOOPBACK);
        }

        @Override
        public void setTrafficClass(int tc) throws SocketException {
            requested = tc;
            super.setTrafficClass(tc);
            accepted = true;
        }
    }

    private static final class RefusingSocket extends DatagramSocket {
        RefusingSocket() throws SocketException {
            super(0, LOOPBACK);
        }

        @Override
        public void setTrafficClass(int tc) throws SocketException {
            throw new SocketException("traffic class " + tc + " refused");
        }
    }

    private static void assertCarries(DatagramSocket sender, DatagramSocket receiver) throws IOException {
        final byte[] sent = {0x47, 0x53, 1, 2, 3};
        sender.send(new DatagramPacket(sent, sent.length, LOOPBACK, receiver.getLocalPort()));
        receiver.setSoTimeout(5000);
        final DatagramPacket packet = new DatagramPacket(new byte[64], 64);
        receiver.receive(packet);
        assertArrayEquals(sent, Arrays.copyOf(packet.getData(), packet.getLength()));
    }

    @Test
    @DisplayName("ADR-0114: the WiFi direct socket asks for traffic class 0xA0, DSCP CS5 in the video queue")
    void theSocketAsksForTheVideoTrafficClass() throws IOException {
        try (RecordingSocket socket = new RecordingSocket(); DatagramSocket peer = new DatagramSocket(0, LOOPBACK)) {
            final boolean marked = TrafficClass.markAsVideo(socket);
            assertEquals(0xA0, socket.requested);
            assertEquals(socket.accepted, marked);
            assertCarries(socket, peer);
            assertCarries(peer, socket);
        }
    }

    @Test
    @DisplayName("ADR-0114: a socket that refuses the traffic class is left as it was and still carries datagrams")
    void aRefusedTrafficClassIsSwallowed() throws IOException {
        try (RefusingSocket socket = new RefusingSocket(); DatagramSocket peer = new DatagramSocket(0, LOOPBACK)) {
            assertFalse(TrafficClass.markAsVideo(socket));
            assertFalse(socket.isClosed());
            assertCarries(socket, peer);
            assertCarries(peer, socket);
        }
    }
}
