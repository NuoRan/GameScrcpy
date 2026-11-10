package com.gamescrcpy.server.session;

import android.os.Build;
import com.gamescrcpy.protocol.Enums.AudioStatus;
import com.gamescrcpy.protocol.Messages;
import com.gamescrcpy.protocol.Messages.Welcome;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog;
import com.gamescrcpy.server.wrappers.Displays;
import com.gamescrcpy.server.wrappers.PlatformException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.function.LongSupplier;

public final class BuildDeviceDescriptor implements DeviceDescriptor {

    private static final int MAX_DISPLAYS = 16;

    private final String serverVersion;
    private final LongSupplier capabilities;

    public BuildDeviceDescriptor(String serverVersion, LongSupplier capabilities) {
        this.serverVersion = serverVersion;
        this.capabilities = capabilities;
    }

    @Override
    public Welcome describe() {
        final Welcome welcome = new Welcome();
        welcome.serverVersion = limit(serverVersion, 32);
        welcome.manufacturer = limit(Build.MANUFACTURER, 64);
        welcome.model = limit(Build.MODEL, 64);
        welcome.androidRelease = limit(Build.VERSION.RELEASE, 32);
        welcome.sdkInt = Build.VERSION.SDK_INT;
        welcome.serialNo = limit(systemProperty("ro.serialno"), 64);
        try {
            final List<DisplaySnapshot> displays = Displays.get().list();
            for (int i = 0; i < displays.size() && i < MAX_DISPLAYS; ++i) {
                welcome.displays.add(displays.get(i).toProtocol());
            }
        } catch (PlatformException e) {
            Log.w("cannot list displays", e);
        }
        for (EncoderCatalog.Encoder encoder : EncoderCatalog.available()) {
            final Messages.Encoder entry = new Messages.Encoder();
            entry.codec = encoder.codec();
            entry.name = limit(encoder.name(), 128);
            entry.hardware = encoder.hardware();
            welcome.encoders.add(entry);
        }
        welcome.audioStatus = Build.VERSION.SDK_INT >= 30 ? AudioStatus.AVAILABLE : AudioStatus.UNSUPPORTED_ANDROID;
        welcome.capabilities = capabilities.getAsLong();
        return welcome;
    }

    static String limit(String text, int maxBytes) {
        if (text == null) {
            return "";
        }
        String result = text;
        while (result.getBytes(StandardCharsets.UTF_8).length > maxBytes) {
            result = result.substring(0, result.offsetByCodePoints(result.length(), -1));
        }
        return result;
    }

    private static String systemProperty(String name) {
        try {
            final Method get = Class.forName("android.os.SystemProperties").getMethod("get", String.class);
            final Object value = get.invoke(null, name);
            return value instanceof String text ? text : "";
        } catch (ReflectiveOperationException e) {
            return "";
        }
    }
}
