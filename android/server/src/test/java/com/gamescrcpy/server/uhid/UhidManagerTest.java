package com.gamescrcpy.server.uhid;

import static com.gamescrcpy.server.uhid.UhidEventsTest.bytes;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.UhidCreate;
import com.gamescrcpy.protocol.Messages.UhidDestroy;
import com.gamescrcpy.protocol.Messages.UhidFeature;
import com.gamescrcpy.protocol.Messages.UhidInput;
import com.gamescrcpy.protocol.Messages.UhidOutput;
import com.gamescrcpy.protocol.Messages.UhidStatus;
import com.gamescrcpy.server.wrappers.InputDevices;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class UhidManagerTest {

    private static final String NAME = "GameScrcpy Touch";
    private static final int VENDOR = 0x1209;
    private static final int PRODUCT = 0x0001;

    private final List<FakeUhidFile> opened = new ArrayList<>();
    private final BlockingQueue<UhidOutput> outputs = new LinkedBlockingQueue<>();
    private final BlockingQueue<UhidStatus> statuses = new LinkedBlockingQueue<>();
    private final FakeInputs inputs = new FakeInputs();
    private final AtomicInteger pauses = new AtomicInteger();
    private final UhidManager manager = new UhidManager(this::open, this::sent, inputs, this::pause);

    private static final class FakeInputs implements UhidManager.Inputs {
        final List<InputDevices.Device> devices = new CopyOnWriteArrayList<>();
        final Set<Integer> vibrating = ConcurrentHashMap.newKeySet();
        volatile Runnable onPause = () -> { };

        @Override
        public List<InputDevices.Device> list() {
            return List.copyOf(devices);
        }

        @Override
        public boolean vibrates(int id) {
            return vibrating.contains(id);
        }
    }

    @AfterEach
    void stopReaders() {
        manager.connectionLost();
    }

    private UhidFile open() {
        final FakeUhidFile file = new FakeUhidFile();
        opened.add(file);
        return file;
    }

    private void sent(Message message) {
        if (message instanceof UhidOutput output) {
            outputs.add(output);
        } else if (message instanceof UhidStatus status) {
            statuses.add(status);
        }
    }

    private void pause(long millis) {
        assertEquals(UhidManager.STATUS_STEP_MS, millis);
        pauses.incrementAndGet();
        inputs.onPause.run();
    }

    private static UhidCreate create(int id, byte[] descriptor) {
        final UhidCreate message = new UhidCreate();
        message.id = id;
        message.vendorId = VENDOR;
        message.productId = PRODUCT;
        message.name = NAME;
        message.reportDescriptor = descriptor;
        return message;
    }

    private static UhidCreate create(int id) {
        return create(id, HidDescriptorTest.touchDescriptor());
    }

    private static UhidInput input(int id, byte[] report) {
        final UhidInput message = new UhidInput();
        message.id = id;
        message.report = report;
        return message;
    }

    private static UhidDestroy destroy(int id) {
        final UhidDestroy message = new UhidDestroy();
        message.id = id;
        return message;
    }

    private static UhidFeature feature(int id, int reportNumber, byte[] data) {
        final UhidFeature message = new UhidFeature();
        message.id = id;
        message.reportNumber = reportNumber;
        message.data = data;
        return message;
    }

    private static ByteBuffer kernelEvent(int type, int size) {
        return ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN).putInt(type);
    }

    private static byte[] getReport(int request, int number, int type) {
        return kernelEvent(UhidEvents.GET_REPORT, UhidEvents.REPORT_REQUEST_SIZE).putInt(request).put((byte) number)
                .put((byte) type).array();
    }

    private static byte[] setReport(int request, int number, byte[] data) {
        return kernelEvent(UhidEvents.SET_REPORT, UhidEvents.REPORT_REQUEST_SIZE + 2 + data.length).putInt(request)
                .put((byte) number).put((byte) UhidEvents.FEATURE_REPORT).putShort((short) data.length).put(data)
                .array();
    }

    private static byte[] output(byte[] data) {
        final ByteBuffer event = kernelEvent(UhidEvents.OUTPUT, UhidEvents.OUTPUT_EVENT_SIZE).put(data);
        event.putShort(UhidEvents.OUTPUT_SIZE_AT, (short) data.length);
        event.put(UhidEvents.OUTPUT_SIZE_AT + 2, (byte) UhidEvents.OUTPUT_REPORT);
        return event.array();
    }

    @Test
    void creatingWritesCreate2AndTheReaderAnswersTheKernelAtOnce() throws Exception {
        manager.create(create(1));
        assertEquals(1, opened.size());
        final FakeUhidFile file = opened.get(0);
        assertArrayEquals(UhidEvents.create2(NAME, UhidCreate.BUS_USB, VENDOR, PRODUCT,
                HidDescriptorTest.touchDescriptor()), file.nextWrite());

        file.event(kernelEvent(UhidEvents.START, 12).array());
        file.event(kernelEvent(UhidEvents.OPEN, 4).array());
        file.event(getReport(77, 2, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(77, 0, bytes(2, 16)), file.nextWrite());
        file.event(getReport(78, 3, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(78, UhidEvents.EIO, new byte[0]), file.nextWrite());
        file.event(getReport(79, 2, UhidEvents.INPUT_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(79, UhidEvents.EIO, new byte[0]), file.nextWrite());
        file.event(setReport(80, 2, bytes(2, 1)));
        assertArrayEquals(UhidEvents.setReportReply(80, 0), file.nextWrite());
        file.event(kernelEvent(UhidEvents.CLOSE, 4).array());
        file.event(output(bytes(1, 2, 3)));
        final UhidOutput sent = outputs.poll(5, TimeUnit.SECONDS);
        assertNotNull(sent);
        assertEquals(1, sent.id);
        assertArrayEquals(bytes(1, 2, 3), sent.data);
        assertNull(file.writtenSoFar());
    }

    @Test
    void create2CarriesTheBusOfTheMessage() throws Exception {
        final UhidCreate pad = create(4, HidDescriptorTest.gamepadDescriptor());
        pad.bus = UhidCreate.BUS_BLUETOOTH;
        manager.create(pad);
        assertArrayEquals(UhidEvents.create2(NAME, UhidCreate.BUS_BLUETOOTH, VENDOR, PRODUCT,
                HidDescriptorTest.gamepadDescriptor()), opened.get(0).nextWrite());
    }

    @Test
    void featureReportsGivenBeforeCreationAnswerTheKernelBeforeTheDescriptor() throws Exception {
        manager.feature(feature(5, 0x02, bytes(0x02, 0x11, 0x22)));
        manager.feature(feature(5, 0x81, bytes(0x81, 1, 2, 3, 4, 5, 6)));
        manager.feature(feature(5, 0x02, bytes(0x02, 0x33)));
        manager.feature(feature(6, 0xA3, bytes(0xA3, 0x44)));
        manager.create(create(5));
        final FakeUhidFile file = opened.get(0);
        assertNotNull(file.nextWrite());

        file.event(getReport(1, 0x02, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(1, 0, bytes(0x02, 0x33)), file.nextWrite());
        file.event(getReport(2, 0x81, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(2, 0, bytes(0x81, 1, 2, 3, 4, 5, 6)), file.nextWrite());
        file.event(getReport(3, 0xA3, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(3, UhidEvents.EIO, new byte[0]), file.nextWrite());
        file.event(getReport(4, 0x81, UhidEvents.INPUT_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(4, UhidEvents.EIO, new byte[0]), file.nextWrite());
        file.event(getReport(5, 0x81, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(5, 0, bytes(0x81, 1, 2, 3, 4, 5, 6)), file.nextWrite());
    }

    @Test
    void featureReportsOfADestroyedOrLostDeviceAreDropped() throws Exception {
        manager.feature(feature(7, 0x02, bytes(0x02, 0x55)));
        manager.destroy(destroy(7));
        manager.create(create(7, HidDescriptorTest.gamepadDescriptor()));
        final FakeUhidFile first = opened.get(0);
        assertNotNull(first.nextWrite());
        first.event(getReport(9, 0x02, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(9, UhidEvents.EIO, new byte[0]), first.nextWrite());

        manager.feature(feature(8, 0x02, bytes(0x02, 0x66)));
        manager.connectionLost();
        manager.create(create(8, HidDescriptorTest.gamepadDescriptor()));
        final FakeUhidFile second = opened.get(1);
        assertNotNull(second.nextWrite());
        second.event(getReport(10, 0x02, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(10, UhidEvents.EIO, new byte[0]), second.nextWrite());
    }

    @Test
    void aDeviceKeepsAtMostSixteenFeatureReports() throws Exception {
        for (int number = 0; number <= UhidManager.MAX_FEATURES; ++number) {
            manager.feature(feature(3, number, bytes(number)));
        }
        manager.create(create(3, HidDescriptorTest.gamepadDescriptor()));
        final FakeUhidFile file = opened.get(0);
        assertNotNull(file.nextWrite());
        file.event(getReport(1, UhidManager.MAX_FEATURES - 1, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(1, 0, bytes(UhidManager.MAX_FEATURES - 1)), file.nextWrite());
        file.event(getReport(2, UhidManager.MAX_FEATURES, UhidEvents.FEATURE_REPORT));
        assertArrayEquals(UhidEvents.getReportReply(2, UhidEvents.EIO, new byte[0]), file.nextWrite());
    }

    @Test
    void aNewInputDeviceWithTheSameIdsIsReportedWithItsVibrator() throws Exception {
        inputs.devices.add(new InputDevices.Device(3, 0x045E, 0x0B13));
        inputs.onPause = () -> {
            if (pauses.get() == 2) {
                inputs.vibrating.add(11);
                inputs.devices.add(new InputDevices.Device(11, VENDOR, PRODUCT));
            }
        };
        manager.create(create(1));
        final UhidStatus status = statuses.poll(5, TimeUnit.SECONDS);
        assertNotNull(status);
        assertEquals(1, status.id);
        assertTrue(status.recognized);
        assertTrue(status.vibration);
        assertEquals(2, pauses.get());
        assertNull(statuses.poll(200, TimeUnit.MILLISECONDS));
    }

    @Test
    void aDeviceThatNeverShowsUpIsReportedUnrecognizedAfterTheLimit() throws Exception {
        inputs.devices.add(new InputDevices.Device(4, VENDOR, PRODUCT));
        inputs.vibrating.add(4);
        manager.create(create(2));
        final UhidStatus status = statuses.poll(5, TimeUnit.SECONDS);
        assertNotNull(status);
        assertEquals(2, status.id);
        assertFalse(status.recognized);
        assertFalse(status.vibration);
        assertEquals(UhidManager.STATUS_LIMIT_MS / UhidManager.STATUS_STEP_MS, pauses.get());
    }

    @Test
    void aDeviceThatCannotBeMadeIsReportedUnrecognizedAtOnce() throws Exception {
        final UhidManager refusing = new UhidManager(() -> {
            final FakeUhidFile file = new FakeUhidFile();
            file.refuseWrites = true;
            return file;
        }, this::sent, inputs, this::pause);
        refusing.create(create(6));
        final UhidStatus status = statuses.poll(5, TimeUnit.SECONDS);
        assertNotNull(status);
        assertEquals(6, status.id);
        assertFalse(status.recognized);
        assertFalse(status.vibration);
        assertEquals(0, pauses.get());
    }

    @Test
    void withoutAListOfInputDevicesEveryDeviceCountsAsRecognized() throws Exception {
        final UhidManager blind = new UhidManager(this::open, this::sent, null, this::pause);
        blind.create(create(9));
        final UhidStatus status = statuses.poll(5, TimeUnit.SECONDS);
        assertNotNull(status);
        assertEquals(9, status.id);
        assertTrue(status.recognized);
        assertFalse(status.vibration);
        assertEquals(0, pauses.get());
        blind.connectionLost();
    }

    @Test
    void inputsGoToTheirDeviceAndUnknownDevicesAreIgnored() throws Exception {
        manager.create(create(1));
        final FakeUhidFile file = opened.get(0);
        assertNotNull(file.nextWrite());
        manager.input(input(2, bytes(1, 0)));
        manager.input(input(1, bytes(1, 1, 5)));
        assertArrayEquals(UhidEvents.input2(bytes(1, 1, 5)), file.nextWrite());
        manager.destroy(destroy(2));
        assertNull(file.writtenSoFar());
        assertFalse(file.closed());
    }

    @Test
    void creatingAnExistingIdReplacesTheDevice() throws Exception {
        manager.create(create(1));
        manager.create(create(1));
        assertEquals(2, opened.size());
        final FakeUhidFile first = opened.get(0);
        final FakeUhidFile second = opened.get(1);
        assertEquals(UhidEvents.CREATE2, UhidEvents.type(first.nextWrite()));
        assertArrayEquals(UhidEvents.destroy(), first.nextWrite());
        assertTrue(first.closed());
        assertEquals(UhidEvents.CREATE2, UhidEvents.type(second.nextWrite()));
        assertFalse(second.closed());
        manager.input(input(1, bytes(1, 0)));
        assertArrayEquals(UhidEvents.input2(bytes(1, 0)), second.nextWrite());
        assertNull(first.writtenSoFar());
    }

    @Test
    void destroyingWritesDestroyAndClosesTheDevice() throws Exception {
        manager.create(create(3));
        final FakeUhidFile file = opened.get(0);
        assertNotNull(file.nextWrite());
        manager.destroy(destroy(3));
        assertArrayEquals(UhidEvents.destroy(), file.nextWrite());
        assertTrue(file.closed());
        manager.input(input(3, bytes(1, 0)));
        manager.destroy(destroy(3));
        assertNull(file.writtenSoFar());
    }

    @Test
    void losingTheConnectionReleasesThenDestroysEveryDevice() throws Exception {
        manager.create(create(1));
        manager.create(create(2));
        manager.create(create(3, bytes(0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15,
                0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0)));
        final FakeUhidFile touched = opened.get(0);
        final FakeUhidFile idle = opened.get(1);
        final FakeUhidFile keyboard = opened.get(2);
        assertNotNull(touched.nextWrite());
        assertNotNull(idle.nextWrite());
        assertNotNull(keyboard.nextWrite());
        final byte[] report = new byte[98];
        report[0] = 1;
        report[1] = 1;
        report[2] = 1;
        report[3] = 1;
        report[5] = 0x40;
        manager.input(input(1, report));
        assertArrayEquals(UhidEvents.input2(report), touched.nextWrite());
        manager.input(input(3, bytes(0x02)));
        assertArrayEquals(UhidEvents.input2(bytes(0x02)), keyboard.nextWrite());

        manager.connectionLost();
        final byte[] released = new byte[98];
        released[0] = 1;
        assertArrayEquals(UhidEvents.input2(released), touched.nextWrite());
        assertArrayEquals(UhidEvents.destroy(), touched.nextWrite());
        assertTrue(touched.closed());
        assertArrayEquals(UhidEvents.destroy(), idle.nextWrite());
        assertTrue(idle.closed());
        assertArrayEquals(UhidEvents.input2(bytes(0x00)), keyboard.nextWrite());
        assertArrayEquals(UhidEvents.destroy(), keyboard.nextWrite());
        assertTrue(keyboard.closed());

        manager.input(input(1, report));
        assertNull(touched.writtenSoFar());
    }

    @Test
    void losingTheConnectionDestroysAGamepadWithoutAnAllZeroReport() throws Exception {
        manager.create(create(2, HidDescriptorTest.gamepadDescriptor()));
        final FakeUhidFile pad = opened.get(0);
        assertNotNull(pad.nextWrite());
        manager.input(input(2, bytes(1, 0x01, 0, 0, 0x80)));
        assertArrayEquals(UhidEvents.input2(bytes(1, 0x01, 0, 0, 0x80)), pad.nextWrite());
        manager.connectionLost();
        assertArrayEquals(UhidEvents.destroy(), pad.nextWrite());
        assertTrue(pad.closed());
        assertNull(pad.writtenSoFar());
    }

    @Test
    void aSessionHoldsAtMostEightDevices() {
        for (int id = 1; id <= UhidManager.MAX_DEVICES + 1; ++id) {
            manager.create(create(id));
        }
        assertEquals(UhidManager.MAX_DEVICES, opened.size());
        manager.create(create(1));
        assertEquals(UhidManager.MAX_DEVICES + 1, opened.size());
        assertTrue(opened.get(0).closed());
        assertFalse(opened.get(UhidManager.MAX_DEVICES).closed());
    }

    @Test
    void aDeviceTheKernelRefusesIsClosedAndForgotten() {
        final List<FakeUhidFile> refused = new ArrayList<>();
        final UhidManager refusing = new UhidManager(() -> {
            final FakeUhidFile file = new FakeUhidFile();
            file.refuseWrites = true;
            refused.add(file);
            return file;
        }, this::sent, inputs, this::pause);
        refusing.create(create(1));
        assertEquals(1, refused.size());
        assertTrue(refused.get(0).closed());
        refusing.input(input(1, bytes(1, 0)));
        refusing.connectionLost();
        assertNull(refused.get(0).writtenSoFar());
    }

    @Test
    void availabilityOpensTheDeviceOnceAndClosesIt() {
        final List<FakeUhidFile> probes = new ArrayList<>();
        assertTrue(UhidManager.available(() -> {
            final FakeUhidFile file = new FakeUhidFile();
            probes.add(file);
            return file;
        }));
        assertEquals(1, probes.size());
        assertTrue(probes.get(0).closed());
        assertFalse(UhidManager.available(() -> {
            throw new IOException("no /dev/uhid here");
        }));
    }
}
