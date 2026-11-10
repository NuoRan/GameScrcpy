package com.gamescrcpy.server.uhid;

import com.gamescrcpy.protocol.Messages.UhidCreate;
import com.gamescrcpy.protocol.Messages.UhidOutput;
import com.gamescrcpy.server.Log;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

final class UhidDevice {

    static final int MOST_OUTPUT_BYTES = 512;

    private static final int READ_BUFFER_SIZE = 8192;

    private final int id;
    private final UhidFile file;
    private final HidDescriptor descriptor;
    private final Map<Integer, byte[]> features;
    private final UhidManager.Client client;
    private volatile boolean destroyed;
    private int lastReportSize;
    private byte lastReportId;
    private boolean inputFailed;

    private UhidDevice(int id, UhidFile file, HidDescriptor descriptor, Map<Integer, byte[]> features,
                       UhidManager.Client client) {
        this.id = id;
        this.file = file;
        this.descriptor = descriptor;
        this.features = features;
        this.client = client;
    }

    static UhidDevice create(UhidFile.Opener opener, UhidCreate message, Map<Integer, byte[]> features,
                             UhidManager.Client client) throws IOException {
        final UhidFile file = opener.open();
        final HidDescriptor descriptor;
        try {
            descriptor = new HidDescriptor(message.reportDescriptor);
            file.write(UhidEvents.create2(message.name, message.bus, message.vendorId, message.productId,
                    message.reportDescriptor));
        } catch (IOException | RuntimeException e) {
            file.close();
            throw e;
        }
        final UhidDevice device = new UhidDevice(message.id, file, descriptor, new HashMap<>(features), client);
        final Thread reader = new Thread(device::readEvents, "gs-uhid-" + message.id);
        reader.setDaemon(true);
        reader.start();
        return device;
    }

    int id() {
        return id;
    }

    boolean destroyed() {
        return destroyed;
    }

    void input(byte[] report) {
        try {
            file.write(UhidEvents.input2(report));
            lastReportSize = report.length;
            lastReportId = report.length > 0 ? report[0] : 0;
        } catch (IOException e) {
            if (!inputFailed) {
                inputFailed = true;
                Log.w("UHID device " + id + " refused an input report", e);
            }
        }
    }

    void release() {
        if (lastReportSize == 0 || descriptor.gamepad()) {
            return;
        }
        final byte[] released = new byte[lastReportSize];
        if (descriptor.numbered()) {
            released[0] = lastReportId;
        }
        try {
            file.write(UhidEvents.input2(released));
        } catch (IOException e) {
            Log.w("cannot release UHID device " + id, e);
        }
    }

    void destroy() {
        destroyed = true;
        try {
            file.write(UhidEvents.destroy());
        } catch (IOException e) {
            Log.w("cannot destroy UHID device " + id, e);
        } finally {
            file.close();
        }
    }

    private void readEvents() {
        final byte[] buffer = new byte[READ_BUFFER_SIZE];
        try {
            while (true) {
                final int size = file.read(buffer);
                if (size < 0) {
                    return;
                }
                answer(buffer, size);
            }
        } catch (IOException | RuntimeException e) {
            Log.w("UHID device " + id + " stopped listening to the kernel", e);
        }
    }

    private void answer(byte[] event, int size) throws IOException {
        if (size < UhidEvents.TYPE_SIZE) {
            return;
        }
        switch (UhidEvents.type(event)) {
            case UhidEvents.OUTPUT -> output(event, size);
            case UhidEvents.GET_REPORT -> getReport(event, size);
            case UhidEvents.SET_REPORT -> setReport(event, size);
            default -> {
            }
        }
    }

    private void output(byte[] event, int size) {
        if (size < UhidEvents.OUTPUT_EVENT_SIZE) {
            return;
        }
        final byte[] data = UhidEvents.outputData(event);
        if (data.length > MOST_OUTPUT_BYTES) {
            Log.w("UHID device " + id + " sent an output report of " + data.length + " bytes, more than the client takes");
            return;
        }
        final UhidOutput output = new UhidOutput();
        output.id = id;
        output.data = data;
        client.send(output);
    }

    private void getReport(byte[] event, int size) throws IOException {
        if (size < UhidEvents.REPORT_REQUEST_SIZE) {
            return;
        }
        final int request = UhidEvents.requestId(event);
        final byte[] report = UhidEvents.reportType(event) == UhidEvents.FEATURE_REPORT
                ? featureReport(UhidEvents.reportNumber(event))
                : null;
        file.write(report != null
                ? UhidEvents.getReportReply(request, 0, report)
                : UhidEvents.getReportReply(request, UhidEvents.EIO, new byte[0]));
    }

    private byte[] featureReport(int number) {
        final byte[] given = features.get(number);
        return given != null ? given.clone() : descriptor.featureReport(number);
    }

    private void setReport(byte[] event, int size) throws IOException {
        if (size < UhidEvents.REPORT_REQUEST_SIZE) {
            return;
        }
        file.write(UhidEvents.setReportReply(UhidEvents.requestId(event), 0));
    }
}
