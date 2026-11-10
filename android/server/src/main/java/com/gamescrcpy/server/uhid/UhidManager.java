package com.gamescrcpy.server.uhid;

import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.UhidCreate;
import com.gamescrcpy.protocol.Messages.UhidDestroy;
import com.gamescrcpy.protocol.Messages.UhidFeature;
import com.gamescrcpy.protocol.Messages.UhidInput;
import com.gamescrcpy.protocol.Messages.UhidStatus;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.InputDevices;
import com.gamescrcpy.server.wrappers.PlatformException;
import java.io.IOException;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class UhidManager {

    public interface Client {
        void send(Message message);
    }

    public interface Inputs {
        List<InputDevices.Device> list();

        boolean vibrates(int id);
    }

    public interface Pause {
        void pause(long millis) throws InterruptedException;
    }

    public static final int MAX_DEVICES = 8;
    public static final int MAX_FEATURES = 16;
    public static final long STATUS_LIMIT_MS = 1500;
    public static final long STATUS_STEP_MS = 100;

    private final UhidFile.Opener opener;
    private final Client client;
    private final Inputs inputs;
    private final Pause pause;
    private final Map<Integer, UhidDevice> devices = new LinkedHashMap<>();
    private final Map<Integer, Map<Integer, byte[]>> features = new LinkedHashMap<>();

    public UhidManager(UhidFile.Opener opener, Client client, Inputs inputs, Pause pause) {
        this.opener = opener;
        this.client = client;
        this.inputs = inputs;
        this.pause = pause;
    }

    public static UhidManager onPhone(UhidFile.Opener opener, Client client) {
        Inputs inputs = null;
        try {
            final InputDevices devices = InputDevices.create();
            inputs = new Inputs() {
                @Override
                public List<InputDevices.Device> list() {
                    return devices.list();
                }

                @Override
                public boolean vibrates(int id) {
                    return devices.vibrates(id);
                }
            };
        } catch (PlatformException e) {
            Log.w("input devices cannot be listed: new UHID devices are taken as recognized", e);
        }
        return new UhidManager(opener, client, inputs, millis -> Thread.sleep(millis)); // source-rule-allow R9: the phone gets 1.5 s to show a new UHID device, looked for every 100 ms
    }

    public static boolean available(UhidFile.Opener opener) {
        try {
            opener.open().close();
            return true;
        } catch (IOException | RuntimeException e) {
            Log.w("no UHID devices: touches can only be injected", e);
            return false;
        }
    }

    public void feature(UhidFeature message) {
        Map<Integer, byte[]> table = features.get(message.id);
        if (table == null) {
            if (features.size() >= MAX_DEVICES) {
                Log.w("feature report " + message.reportNumber + " of UHID device " + message.id
                        + " dropped: too many devices wait for theirs");
                return;
            }
            table = new LinkedHashMap<>();
            features.put(message.id, table);
        }
        if (!table.containsKey(message.reportNumber) && table.size() >= MAX_FEATURES) {
            Log.w("feature report " + message.reportNumber + " of UHID device " + message.id
                    + " dropped: a device keeps at most " + MAX_FEATURES);
            return;
        }
        table.put(message.reportNumber, message.data.clone());
    }

    public void create(UhidCreate message) {
        final Map<Integer, byte[]> given = features.remove(message.id);
        final UhidDevice replaced = devices.remove(message.id);
        if (replaced != null) {
            replaced.destroy();
        }
        if (devices.size() >= MAX_DEVICES) {
            Log.w("UHID device " + message.id + " not made: a session has at most " + MAX_DEVICES);
            report(message.id, false, false);
            return;
        }
        final Set<Integer> before = present();
        final UhidDevice device;
        try {
            device = UhidDevice.create(opener, message, given != null ? given : Map.of(), client);
        } catch (IOException | RuntimeException e) {
            Log.w("cannot make UHID device " + message.id, e);
            report(message.id, false, false);
            return;
        }
        devices.put(message.id, device);
        Log.i("UHID device " + message.id + " made: " + message.name);
        watch(device, message.vendorId, message.productId, before);
    }

    public void input(UhidInput message) {
        final UhidDevice device = devices.get(message.id);
        if (device != null) {
            device.input(message.report);
        }
    }

    public void destroy(UhidDestroy message) {
        features.remove(message.id);
        final UhidDevice device = devices.remove(message.id);
        if (device != null) {
            device.destroy();
            Log.i("UHID device " + message.id + " destroyed");
        }
    }

    public void connectionLost() {
        features.clear();
        for (UhidDevice device : devices.values()) {
            device.release();
            device.destroy();
        }
        devices.clear();
    }

    private Set<Integer> present() {
        final Set<Integer> ids = new HashSet<>();
        if (inputs == null) {
            return ids;
        }
        try {
            for (InputDevices.Device each : inputs.list()) {
                ids.add(each.id());
            }
        } catch (RuntimeException e) {
            Log.w("cannot list the input devices before making a UHID device", e);
        }
        return ids;
    }

    private void watch(UhidDevice device, int vendorId, int productId, Set<Integer> before) {
        final Thread watcher = new Thread(() -> lookFor(device, vendorId, productId, before),
                "gs-uhid-status-" + device.id());
        watcher.setDaemon(true);
        watcher.start();
    }

    private void lookFor(UhidDevice device, int vendorId, int productId, Set<Integer> before) {
        if (inputs == null) {
            reportUnlessGone(device, true, false);
            return;
        }
        long waited = 0;
        while (!device.destroyed()) {
            final Integer found = appeared(vendorId, productId, before);
            if (found != null) {
                reportUnlessGone(device, true, vibrates(found));
                return;
            }
            if (waited >= STATUS_LIMIT_MS) {
                Log.w("UHID device " + device.id() + " did not show up among the phone's input devices");
                reportUnlessGone(device, false, false);
                return;
            }
            try {
                pause.pause(STATUS_STEP_MS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            }
            waited += STATUS_STEP_MS;
        }
    }

    private Integer appeared(int vendorId, int productId, Set<Integer> before) {
        try {
            for (InputDevices.Device each : inputs.list()) {
                if (!before.contains(each.id()) && each.vendorId() == vendorId && each.productId() == productId) {
                    return each.id();
                }
            }
        } catch (RuntimeException e) {
            Log.w("cannot list the input devices", e);
        }
        return null;
    }

    private boolean vibrates(int id) {
        try {
            return inputs.vibrates(id);
        } catch (RuntimeException e) {
            return false;
        }
    }

    private void reportUnlessGone(UhidDevice device, boolean recognized, boolean vibration) {
        if (!device.destroyed()) {
            report(device.id(), recognized, vibration);
        }
    }

    private void report(int id, boolean recognized, boolean vibration) {
        final UhidStatus status = new UhidStatus();
        status.id = id;
        status.recognized = recognized;
        status.vibration = vibration;
        client.send(status);
    }
}
