package com.genymobile.scrcpy.control;

import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

public final class DeviceMessageSender {

    private final IControlChannel controlChannel;
    private Thread thread;
    private final BlockingQueue<DeviceMessage> queue = new ArrayBlockingQueue<>(16);

    public DeviceMessageSender(IControlChannel controlChannel) {
        this.controlChannel = controlChannel;
    }

    public void send(DeviceMessage msg) {
        queue.offer(msg);
    }

    private void loop() throws IOException, InterruptedException {
        while (!Thread.currentThread().isInterrupted()) {
            DeviceMessage msg = queue.take();
            controlChannel.send(msg);
        }
    }

    public void start() {
        thread = new Thread(() -> {
            try {
                loop();
            } catch (IOException | InterruptedException e) {
                // expected on close
            }
        }, "control-send");
        thread.start();
    }

    public void stop() {
        if (thread != null) {
            thread.interrupt();
        }
    }

    public void join() throws InterruptedException {
        if (thread != null) {
            thread.join();
        }
    }
}
