package com.genymobile.scrcpy.control;

import java.io.IOException;

/**
 * Interface for control channel communication
 * Supports both LocalSocket (TCP) and KCP (UDP) implementations
 */
public interface IControlChannel {
    /**
     * Receive a control message from the client
     */
    ControlMessage recv() throws IOException;

    /**
     * Send a device message to the client
     */
    void send(DeviceMessage msg) throws IOException;

    /**
     * Close the control channel
     */
    default void close() {}
}
