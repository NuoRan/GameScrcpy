package com.gamescrcpy.server.device;

import android.os.BatteryManager;
import com.gamescrcpy.protocol.Messages.DeviceStatus;
import com.gamescrcpy.server.Log;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

public final class DeviceStatusSampler implements AutoCloseable {

    public interface Battery {
        BatteryReading read();
    }

    public interface CpuTimes {
        String read() throws IOException;
    }

    public interface Sink {
        void send(DeviceStatus status);
    }

    public record BatteryReading(boolean present, int level, int scale, int status, int plugged, int temperature) {
    }

    public record CpuSample(long total, long idle) {
    }

    private static final String PROC_STAT = "/proc/stat";
    private static final int CPU_FIELDS = 8;
    private static final int IDLE_FIELD = 3;
    private static final int IOWAIT_FIELD = 4;
    private static final int LEAST_CPU_FIELDS = 4;

    private final Battery battery;
    private final CpuTimes cpu;
    private final Sink sink;
    private final ScheduledExecutorService timer = Executors.newSingleThreadScheduledExecutor(task -> {
        final Thread thread = new Thread(task, "gs-status");
        thread.setDaemon(true);
        return thread;
    });
    private ScheduledFuture<?> running;
    private int periodMs;
    private long generation;
    private CpuSample previous;
    private boolean cpuWarned;

    public DeviceStatusSampler(Battery battery, CpuTimes cpu, Sink sink) {
        this.battery = battery;
        this.cpu = cpu;
        this.sink = sink;
    }

    public synchronized void subscribe(boolean on, int newPeriodMs) {
        if (!on) {
            stop();
            return;
        }
        if (running != null && newPeriodMs == periodMs) {
            return;
        }
        final long firstDelay = running != null ? newPeriodMs : 0;
        if (running != null) {
            running.cancel(false);
        } else {
            previous = null;
        }
        final long scheduled = ++generation;
        periodMs = newPeriodMs;
        try {
            running = timer.scheduleWithFixedDelay(() -> report(scheduled), firstDelay, newPeriodMs,
                    TimeUnit.MILLISECONDS);
        } catch (RejectedExecutionException e) {
            running = null;
            Log.w("the phone's status cannot be sampled any more", e);
        }
    }

    public synchronized void stop() {
        ++generation;
        if (running != null) {
            running.cancel(false);
            running = null;
        }
        previous = null;
    }

    @Override
    public void close() {
        stop();
        timer.shutdownNow();
    }

    synchronized DeviceStatus sample() {
        final DeviceStatus status = new DeviceStatus();
        final BatteryReading reading = battery.read();
        if (reading != null && reading.present()) {
            status.batteryPercent = batteryPercent(reading.level(), reading.scale());
            status.charging = charging(reading.status(), reading.plugged());
            status.temperatureDeciC = temperatureDeciC(reading.temperature());
        }
        final CpuSample current = cpuSample();
        status.cpuPermille = cpuPermille(previous, current);
        previous = current;
        return status;
    }

    public static int batteryPercent(int level, int scale) {
        if (level < 0 || scale <= 0) {
            return DeviceStatus.UNKNOWN_BATTERY;
        }
        return (int) Math.min(DeviceStatus.MAX_BATTERY, (level * 100L + scale / 2) / scale);
    }

    public static boolean charging(int status, int plugged) {
        return status == BatteryManager.BATTERY_STATUS_CHARGING
                || (status == BatteryManager.BATTERY_STATUS_FULL && plugged != 0);
    }

    public static int temperatureDeciC(int temperature) {
        if (temperature <= Short.MIN_VALUE || temperature > Short.MAX_VALUE) {
            return DeviceStatus.UNKNOWN_TEMPERATURE;
        }
        return temperature;
    }

    public static CpuSample parseCpu(String text) {
        if (text == null) {
            return null;
        }
        final int end = text.indexOf('\n');
        final String[] fields = (end >= 0 ? text.substring(0, end) : text).trim().split("\\s+");
        if (fields.length < 1 + LEAST_CPU_FIELDS || !fields[0].equals("cpu")) {
            return null;
        }
        final int counted = Math.min(fields.length - 1, CPU_FIELDS);
        long total = 0;
        long idle = 0;
        try {
            for (int i = 0; i < counted; ++i) {
                final long value = Long.parseLong(fields[i + 1]);
                if (value < 0) {
                    return null;
                }
                total += value;
                if (i == IDLE_FIELD || i == IOWAIT_FIELD) {
                    idle += value;
                }
            }
        } catch (NumberFormatException e) {
            return null;
        }
        return new CpuSample(total, idle);
    }

    public static int cpuPermille(CpuSample before, CpuSample after) {
        if (before == null || after == null) {
            return DeviceStatus.UNKNOWN_CPU;
        }
        final long total = after.total() - before.total();
        final long idle = after.idle() - before.idle();
        if (total <= 0 || idle < 0) {
            return DeviceStatus.UNKNOWN_CPU;
        }
        final long busy = Math.max(0, total - idle);
        return (int) ((busy * DeviceStatus.MAX_CPU + total / 2) / total);
    }

    public static String readProcStat() throws IOException {
        try (BufferedReader reader = new BufferedReader(new FileReader(PROC_STAT))) {
            final String line = reader.readLine();
            if (line == null) {
                throw new IOException(PROC_STAT + " is empty");
            }
            return line;
        }
    }

    private void report(long scheduled) {
        try {
            final DeviceStatus status;
            synchronized (this) {
                if (scheduled != generation) {
                    return;
                }
                status = sample();
            }
            sink.send(status);
        } catch (RuntimeException e) {
            Log.w("cannot report the phone's status", e);
        }
    }

    private CpuSample cpuSample() {
        try {
            final CpuSample sample = parseCpu(cpu.read());
            if (sample == null) {
                warnCpu("the CPU use cannot be read: " + PROC_STAT + " has an unexpected first line", null);
            }
            return sample;
        } catch (IOException | RuntimeException e) {
            warnCpu("the CPU use cannot be read from " + PROC_STAT, e);
            return null;
        }
    }

    private void warnCpu(String message, Exception cause) {
        if (cpuWarned) {
            return;
        }
        cpuWarned = true;
        if (cause == null) {
            Log.w(message);
        } else {
            Log.w(message, cause);
        }
    }
}
