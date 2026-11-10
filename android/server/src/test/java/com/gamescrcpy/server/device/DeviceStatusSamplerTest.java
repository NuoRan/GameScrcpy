package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Messages.DeviceStatus;
import com.gamescrcpy.server.device.DeviceStatusSampler.BatteryReading;
import com.gamescrcpy.server.device.DeviceStatusSampler.CpuSample;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;
import org.junit.jupiter.api.Test;

class DeviceStatusSamplerTest {

    private static final int STATUS_UNKNOWN = 1;
    private static final int STATUS_CHARGING = 2;
    private static final int STATUS_DISCHARGING = 3;
    private static final int STATUS_NOT_CHARGING = 4;
    private static final int STATUS_FULL = 5;
    private static final int PLUGGED_AC = 1;
    private static final int PLUGGED_USB = 2;
    private static final String UNREADABLE = "<unreadable>";
    private static final String FIRST = "cpu  100 0 100 800 0 0 0 0 0 0";
    private static final String SECOND = "cpu  250 0 250 1450 50 0 0 0 0 0";
    private static final BatteryReading CHARGING_AT_82 =
            new BatteryReading(true, 82, 100, STATUS_CHARGING, PLUGGED_AC, 365);

    private static final class Lines implements DeviceStatusSampler.CpuTimes {
        private final Deque<String> lines = new ArrayDeque<>();

        Lines(String... lines) {
            this.lines.addAll(List.of(lines));
        }

        @Override
        public String read() throws IOException {
            final String line = lines.poll();
            if (line == null || line.equals(UNREADABLE)) {
                throw new IOException("cannot read");
            }
            return line;
        }
    }

    private static DeviceStatusSampler sampler(DeviceStatusSampler.Battery battery, Lines cpu) {
        return new DeviceStatusSampler(battery, cpu, status -> {
        });
    }

    @Test
    void theBatteryLevelIsTheShareOfTheScaleRounded() {
        assertEquals(82, DeviceStatusSampler.batteryPercent(82, 100));
        assertEquals(82, DeviceStatusSampler.batteryPercent(41, 50));
        assertEquals(33, DeviceStatusSampler.batteryPercent(1, 3));
        assertEquals(67, DeviceStatusSampler.batteryPercent(2, 3));
        assertEquals(0, DeviceStatusSampler.batteryPercent(0, 100));
        assertEquals(100, DeviceStatusSampler.batteryPercent(100, 100));
        assertEquals(100, DeviceStatusSampler.batteryPercent(150, 100));
        assertEquals(100, DeviceStatusSampler.batteryPercent(Integer.MAX_VALUE, Integer.MAX_VALUE));
        assertEquals(DeviceStatus.UNKNOWN_BATTERY, DeviceStatusSampler.batteryPercent(-1, 100));
        assertEquals(DeviceStatus.UNKNOWN_BATTERY, DeviceStatusSampler.batteryPercent(50, 0));
        assertEquals(DeviceStatus.UNKNOWN_BATTERY, DeviceStatusSampler.batteryPercent(50, -1));
    }

    @Test
    void theTemperatureIsAlreadyInTenthsOfADegreeAndOutOfRangeIsUnknown() {
        assertEquals(365, DeviceStatusSampler.temperatureDeciC(365));
        assertEquals(-125, DeviceStatusSampler.temperatureDeciC(-125));
        assertEquals(0, DeviceStatusSampler.temperatureDeciC(0));
        assertEquals(32767, DeviceStatusSampler.temperatureDeciC(32767));
        assertEquals(-32767, DeviceStatusSampler.temperatureDeciC(-32767));
        assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, DeviceStatusSampler.temperatureDeciC(Integer.MIN_VALUE));
        assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, DeviceStatusSampler.temperatureDeciC(-32768));
        assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, DeviceStatusSampler.temperatureDeciC(32768));
        assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, DeviceStatusSampler.temperatureDeciC(-40000));
    }

    @Test
    void chargingMeansChargingOrFullWhilePluggedIn() {
        assertTrue(DeviceStatusSampler.charging(STATUS_CHARGING, PLUGGED_AC));
        assertTrue(DeviceStatusSampler.charging(STATUS_CHARGING, 0));
        assertTrue(DeviceStatusSampler.charging(STATUS_FULL, PLUGGED_USB));
        assertFalse(DeviceStatusSampler.charging(STATUS_FULL, 0));
        assertFalse(DeviceStatusSampler.charging(STATUS_DISCHARGING, 0));
        assertFalse(DeviceStatusSampler.charging(STATUS_NOT_CHARGING, PLUGGED_AC));
        assertFalse(DeviceStatusSampler.charging(STATUS_UNKNOWN, PLUGGED_AC));
    }

    @Test
    void theFirstLineOfProcStatGivesTheTotalAndTheIdleTime() {
        assertEquals(new CpuSample(9390, 3722),
                DeviceStatusSampler.parseCpu("cpu  4705 356 584 3699 23 23 0 0 0 0\ncpu0 1 2 3 4 5 6 7 8 9 10\n"));
        assertEquals(new CpuSample(200, 100), DeviceStatusSampler.parseCpu("cpu 100 0 0 100 0 0 0 0 50 50"));
        assertEquals(new CpuSample(280, 90), DeviceStatusSampler.parseCpu("cpu 10 20 30 40 50 60 70"));
        assertEquals(new CpuSample(10, 4), DeviceStatusSampler.parseCpu("cpu 1 2 3 4"));
        assertEquals(new CpuSample(10, 4), DeviceStatusSampler.parseCpu("  cpu   1 2 3 4  "));
        assertNull(DeviceStatusSampler.parseCpu(null));
        assertNull(DeviceStatusSampler.parseCpu(""));
        assertNull(DeviceStatusSampler.parseCpu("cpu 1 2 3"));
        assertNull(DeviceStatusSampler.parseCpu("cpu 1 2 3\n4 5 6"));
        assertNull(DeviceStatusSampler.parseCpu("cpu0 1 2 3 4 5"));
        assertNull(DeviceStatusSampler.parseCpu("intr 1 2 3 4 5"));
        assertNull(DeviceStatusSampler.parseCpu("cpu 1 2 x 4 5"));
        assertNull(DeviceStatusSampler.parseCpu("cpu 1 2 3 -4 5"));
    }

    @Test
    void theCpuUseIsTheBusyShareOfTheTimeBetweenTwoSamples() {
        assertEquals(250, DeviceStatusSampler.cpuPermille(new CpuSample(1000, 800), new CpuSample(2000, 1550)));
        assertEquals(333, DeviceStatusSampler.cpuPermille(new CpuSample(0, 0), new CpuSample(3, 2)));
        assertEquals(667, DeviceStatusSampler.cpuPermille(new CpuSample(0, 0), new CpuSample(3, 1)));
        assertEquals(1000, DeviceStatusSampler.cpuPermille(new CpuSample(0, 0), new CpuSample(1000, 0)));
        assertEquals(0, DeviceStatusSampler.cpuPermille(new CpuSample(0, 0), new CpuSample(1000, 1000)));
        assertEquals(0, DeviceStatusSampler.cpuPermille(new CpuSample(0, 0), new CpuSample(1000, 1200)));
        assertEquals(DeviceStatus.UNKNOWN_CPU, DeviceStatusSampler.cpuPermille(null, new CpuSample(1000, 800)));
        assertEquals(DeviceStatus.UNKNOWN_CPU, DeviceStatusSampler.cpuPermille(new CpuSample(1000, 800), null));
        assertEquals(DeviceStatus.UNKNOWN_CPU,
                DeviceStatusSampler.cpuPermille(new CpuSample(1000, 800), new CpuSample(1000, 800)));
        assertEquals(DeviceStatus.UNKNOWN_CPU,
                DeviceStatusSampler.cpuPermille(new CpuSample(2000, 800), new CpuSample(1000, 700)));
        assertEquals(DeviceStatus.UNKNOWN_CPU,
                DeviceStatusSampler.cpuPermille(new CpuSample(1000, 800), new CpuSample(2000, 700)));
    }

    @Test
    void theFirstReportHasNoCpuUseAndTheNextOneDoes() {
        try (DeviceStatusSampler sampler = sampler(() -> CHARGING_AT_82, new Lines(FIRST, SECOND))) {
            final DeviceStatus first = sampler.sample();
            assertEquals(82, first.batteryPercent);
            assertTrue(first.charging);
            assertEquals(365, first.temperatureDeciC);
            assertEquals(DeviceStatus.UNKNOWN_CPU, first.cpuPermille);
            assertTrue(first.isValid());

            final DeviceStatus second = sampler.sample();
            assertEquals(82, second.batteryPercent);
            assertEquals(300, second.cpuPermille);
            assertTrue(second.isValid());
        }
    }

    @Test
    void aPhoneWithoutABatteryOrWithoutTheBroadcastReportsUnknown() {
        final BatteryReading absent = new BatteryReading(false, 100, 100, STATUS_UNKNOWN, PLUGGED_AC, 250);
        try (DeviceStatusSampler sampler = sampler(() -> absent, new Lines(FIRST))) {
            final DeviceStatus status = sampler.sample();
            assertEquals(DeviceStatus.UNKNOWN_BATTERY, status.batteryPercent);
            assertFalse(status.charging);
            assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, status.temperatureDeciC);
            assertTrue(status.isValid());
        }
        try (DeviceStatusSampler sampler = sampler(() -> null, new Lines(FIRST))) {
            final DeviceStatus status = sampler.sample();
            assertEquals(DeviceStatus.UNKNOWN_BATTERY, status.batteryPercent);
            assertFalse(status.charging);
            assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, status.temperatureDeciC);
        }
    }

    @Test
    void anUnreadableProcStatLeavesOnlyTheCpuUseUnknown() {
        try (DeviceStatusSampler sampler = sampler(() -> CHARGING_AT_82, new Lines(UNREADABLE, FIRST, SECOND))) {
            final DeviceStatus unreadable = sampler.sample();
            assertEquals(DeviceStatus.UNKNOWN_CPU, unreadable.cpuPermille);
            assertEquals(82, unreadable.batteryPercent);
            assertEquals(DeviceStatus.UNKNOWN_CPU, sampler.sample().cpuPermille);
            assertEquals(300, sampler.sample().cpuPermille);
        }
        try (DeviceStatusSampler sampler = sampler(() -> CHARGING_AT_82, new Lines("cpu garbage", FIRST))) {
            assertEquals(DeviceStatus.UNKNOWN_CPU, sampler.sample().cpuPermille);
            assertEquals(DeviceStatus.UNKNOWN_CPU, sampler.sample().cpuPermille);
        }
    }

    @Test
    void stoppingForgetsTheLastCpuSample() {
        try (DeviceStatusSampler sampler = sampler(() -> CHARGING_AT_82, new Lines(FIRST, SECOND))) {
            sampler.sample();
            sampler.stop();
            assertEquals(DeviceStatus.UNKNOWN_CPU, sampler.sample().cpuPermille);
            sampler.subscribe(false, 2000);
            sampler.stop();
        }
    }
}
