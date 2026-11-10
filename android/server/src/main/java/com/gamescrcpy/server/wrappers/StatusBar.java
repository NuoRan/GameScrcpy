package com.gamescrcpy.server.wrappers;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.SystemPanelAction;
import com.gamescrcpy.server.device.SystemPanels;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public final class StatusBar implements SystemPanels.Platform {

    private final Object service;
    private final Method notifications;
    private final Method settings;
    private final boolean settingsTakesSubPanel;
    private final Method collapse;

    private StatusBar(Object service, Method notifications, Method settings, boolean settingsTakesSubPanel,
                      Method collapse) {
        this.service = service;
        this.notifications = notifications;
        this.settings = settings;
        this.settingsTakesSubPanel = settingsTakesSubPanel;
        this.collapse = collapse;
    }

    public static StatusBar create() throws PlatformException {
        return of(SystemServices.get("statusbar", "com.android.internal.statusbar.IStatusBarService"));
    }

    public static StatusBar of(Object service) {
        final Class<?> type = service.getClass();
        final Method settingsWithSubPanel = find(type, "expandSettingsPanel", String.class);
        return new StatusBar(service, find(type, "expandNotificationsPanel"),
                settingsWithSubPanel != null ? settingsWithSubPanel : find(type, "expandSettingsPanel"),
                settingsWithSubPanel != null, find(type, "collapsePanels"));
    }

    @Override
    public void show(SystemPanelAction action) throws SystemPanels.Failure {
        final Method method = switch (action) {
            case EXPAND_NOTIFICATIONS -> notifications;
            case EXPAND_QUICK_SETTINGS -> settings;
            case COLLAPSE -> collapse;
        };
        if (method == null) {
            throw new SystemPanels.Failure(ErrorCode.UNSUPPORTED_ANDROID,
                    "this Android version has no way to " + SystemPanels.describe(action));
        }
        try {
            if (method == settings && settingsTakesSubPanel) {
                method.invoke(service, new Object[] {null});
            } else {
                method.invoke(service);
            }
        } catch (InvocationTargetException e) {
            final Throwable cause = e.getCause();
            if (cause instanceof SecurityException) {
                throw new SystemPanels.Failure(ErrorCode.PERMISSION_DENIED,
                        "the phone refused to " + SystemPanels.describe(action) + ": " + cause.getMessage(), cause);
            }
            throw new SystemPanels.Failure(ErrorCode.INTERNAL_ERROR,
                    "the status bar could not " + SystemPanels.describe(action) + ": " + cause, cause);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new SystemPanels.Failure(ErrorCode.INTERNAL_ERROR,
                    "cannot " + SystemPanels.describe(action) + ": " + e, e);
        }
    }

    private static Method find(Class<?> type, String name, Class<?>... parameters) {
        try {
            return type.getMethod(name, parameters);
        } catch (NoSuchMethodException e) {
            return null;
        }
    }
}
