package com.gamescrcpy.server.device;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Messages.AppEntry;
import com.gamescrcpy.protocol.Messages.AppIcon;
import com.gamescrcpy.protocol.Messages.AppList;
import com.gamescrcpy.protocol.Messages.RequestAppIcon;
import com.gamescrcpy.protocol.Messages.RequestAppList;
import com.gamescrcpy.protocol.Messages.StartApp;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.transport.FrameEndpoint;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.IntSupplier;

public final class AppService implements AutoCloseable {

    public static final int MAX_OUTSTANDING = BulkRequests.MAX_OUTSTANDING;
    public static final int MAX_APPS = 4096;
    public static final int MAX_ICON_BYTES = 1024 * 1024;
    public static final int MAIN_DISPLAY = 0;
    public static final int NO_DISPLAY = -1;

    private static final int NAME_LIMIT = 255;
    private static final int MESSAGE_LIMIT = 1000;

    public record App(String packageName, String label, long versionCode, boolean system, boolean enabled,
                      boolean game) {
    }

    public static final class Failure extends BulkRequests.Failure {
        private static final long serialVersionUID = 1L;

        public Failure(ErrorCode code, String message) {
            super(code, message);
        }

        public Failure(ErrorCode code, String message, Throwable cause) {
            super(code, message, cause);
        }
    }

    public interface Platform {
        List<App> launchable(boolean includeSystem) throws Failure;

        byte[] icon(String packageName, int sizePx) throws Failure;

        void start(String packageName, int displayId, boolean forceStop) throws Failure;
    }

    public interface Notices {
        void launchFailed(ErrorCode code, String message);
    }

    private final Platform platform;
    private final IntSupplier videoDisplay;
    private final Notices notices;
    private final BulkRequests bulk;
    private final boolean ownsBulk;
    private final ExecutorService requests = Executors.newSingleThreadExecutor(runnable -> worker(runnable, "gs-bulk"));
    private final ExecutorService launches = Executors.newSingleThreadExecutor(runnable -> worker(runnable, "gs-launch"));

    public AppService(Platform platform, IntSupplier videoDisplay, Notices notices) {
        this(platform, videoDisplay, notices, new BulkRequests(), true);
    }

    public AppService(Platform platform, IntSupplier videoDisplay, Notices notices, BulkRequests bulk) {
        this(platform, videoDisplay, notices, bulk, false);
    }

    private AppService(Platform platform, IntSupplier videoDisplay, Notices notices, BulkRequests bulk,
                       boolean ownsBulk) {
        this.platform = platform;
        this.videoDisplay = videoDisplay;
        this.notices = notices;
        this.bulk = bulk;
        this.ownsBulk = ownsBulk;
    }

    public void bulkOpened(FrameEndpoint endpoint) {
        bulk.bulkOpened(endpoint);
    }

    public void connectionLost() {
        bulk.connectionLost();
    }

    public void list(RequestAppList request) {
        final long requestId = request.requestId;
        final boolean includeSystem = request.includeSystem;
        bulk.submit(requestId, requests, () -> listed(requestId, platform.launchable(includeSystem)));
    }

    public void icon(RequestAppIcon request) {
        final long requestId = request.requestId;
        final String packageName = request.packageName;
        final int sizePx = request.sizePx;
        bulk.submit(requestId, requests, () -> {
            final byte[] png = platform.icon(packageName, sizePx);
            if (png.length > MAX_ICON_BYTES) {
                throw new Failure(ErrorCode.INTERNAL_ERROR, "the icon of " + packageName + " takes " + png.length
                        + " bytes, over the limit");
            }
            final AppIcon icon = new AppIcon();
            icon.requestId = requestId;
            icon.packageName = packageName;
            icon.png = png;
            return icon;
        });
    }

    public void start(StartApp request) {
        final String packageName = request.packageName;
        final boolean forceStop = (request.flags & StartApp.FORCE_STOP) != 0;
        final int displayId = (request.flags & StartApp.ON_VIDEO_DISPLAY) != 0 ? videoDisplay.getAsInt() : MAIN_DISPLAY;
        if (displayId == NO_DISPLAY) {
            launchFailed(ErrorCode.NOT_FOUND, "cannot start " + packageName + " on the video display: no video yet");
            return;
        }
        launches.execute(() -> {
            try {
                platform.start(packageName, displayId, forceStop);
                Log.i("started " + packageName + " on display " + displayId);
            } catch (Failure e) {
                launchFailed(e.code(), e.getMessage());
            } catch (RuntimeException e) {
                launchFailed(ErrorCode.INTERNAL_ERROR, "cannot start " + packageName + ": " + e);
            }
        });
    }

    @Override
    public void close() {
        requests.shutdownNow();
        launches.shutdownNow();
        if (ownsBulk) {
            bulk.close();
        }
    }

    private static AppList listed(long requestId, List<App> apps) {
        final AppList list = new AppList();
        list.requestId = requestId;
        for (App app : apps) {
            if (list.apps.size() == MAX_APPS) {
                break;
            }
            final AppEntry entry = new AppEntry();
            entry.packageName = clip(app.packageName());
            entry.label = clip(app.label());
            entry.versionCode = app.versionCode();
            entry.flags = (app.system() ? AppEntry.SYSTEM : 0) | (app.enabled() ? AppEntry.ENABLED : 0)
                    | (app.game() ? AppEntry.GAME : 0);
            list.apps.add(entry);
        }
        return list;
    }

    private void launchFailed(ErrorCode code, String message) {
        notices.launchFailed(code, message.length() > MESSAGE_LIMIT ? message.substring(0, MESSAGE_LIMIT) : message);
    }

    static String clip(String text) {
        if (text == null) {
            return "";
        }
        String clipped = text;
        while (clipped.getBytes(StandardCharsets.UTF_8).length > NAME_LIMIT) {
            int end = clipped.length() - 1;
            if (end > 0 && Character.isLowSurrogate(clipped.charAt(end))) {
                --end;
            }
            clipped = clipped.substring(0, end);
        }
        return clipped;
    }

    private static Thread worker(Runnable runnable, String name) {
        final Thread thread = new Thread(runnable, name);
        thread.setDaemon(true);
        return thread;
    }
}
