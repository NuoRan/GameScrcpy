package com.gamescrcpy.server.wrappers;

import android.content.Context;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import java.util.ArrayList;
import java.util.List;

public final class InputDevices {

    public record Device(int id, int vendorId, int productId) {
    }

    private final InputManager manager;

    private InputDevices(InputManager manager) {
        this.manager = manager;
    }

    public static InputDevices create() throws PlatformException {
        try {
            final Context context = ProcessEnvironment.context();
            InputManager manager = context != null ? context.getSystemService(InputManager.class) : null;
            if (manager == null) {
                manager = (InputManager) InputManager.class.getMethod("getInstance").invoke(null);
            }
            if (manager == null) {
                throw new PlatformException("the input manager is not running");
            }
            return new InputDevices(manager);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("input devices cannot be listed", e);
        }
    }

    public List<Device> list() {
        final List<Device> devices = new ArrayList<>();
        for (int id : manager.getInputDeviceIds()) {
            final InputDevice device = manager.getInputDevice(id);
            if (device != null) {
                devices.add(new Device(id, device.getVendorId(), device.getProductId()));
            }
        }
        return devices;
    }

    @SuppressWarnings("deprecation")
    public boolean vibrates(int id) {
        final InputDevice device = manager.getInputDevice(id);
        if (device == null) {
            return false;
        }
        try {
            return device.getVibrator().hasVibrator();
        } catch (RuntimeException e) {
            return false;
        }
    }
}
