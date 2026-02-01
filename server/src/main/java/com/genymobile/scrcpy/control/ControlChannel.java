package com.genymobile.scrcpy.control;

import android.net.LocalSocket;

import java.io.IOException;

public final class ControlChannel implements IControlChannel {

    private final ControlMessageReader reader;
    private final DeviceMessageWriter writer;

    public ControlChannel(LocalSocket controlSocket) throws IOException {
        reader = new ControlMessageReader(controlSocket.getInputStream());
        writer = new DeviceMessageWriter(controlSocket.getOutputStream());
    }

    @Override
    public ControlMessage recv() throws IOException {
        return reader.read();
    }

    @Override
    public void send(DeviceMessage msg) throws IOException {
        writer.write(msg);
    }
}
