package com.gamescrcpy.server.transport;

import com.gamescrcpy.server.Log;
import java.net.DatagramSocket;
import java.net.SocketException;

public final class TrafficClass {

    public static final int VIDEO = 0xA0;

    private TrafficClass() {
    }

    public static boolean markAsVideo(DatagramSocket socket) {
        try {
            socket.setTrafficClass(VIDEO);
            return true;
        } catch (SocketException e) {
            Log.d("the WiFi direct socket keeps the default traffic class: " + e.getMessage());
            return false;
        }
    }
}
