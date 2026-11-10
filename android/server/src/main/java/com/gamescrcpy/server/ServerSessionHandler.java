package com.gamescrcpy.server;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.KeyAction;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.BackOrScreenOn;
import com.gamescrcpy.protocol.Messages.ConfigureAudio;
import com.gamescrcpy.protocol.Messages.ConfigureVideo;
import com.gamescrcpy.protocol.Messages.DisplayChanged;
import com.gamescrcpy.protocol.Messages.GetClipboard;
import com.gamescrcpy.protocol.Messages.Key;
import com.gamescrcpy.protocol.Messages.Notice;
import com.gamescrcpy.protocol.Messages.ReceiverReport;
import com.gamescrcpy.protocol.Messages.RequestAppIcon;
import com.gamescrcpy.protocol.Messages.RequestAppList;
import com.gamescrcpy.protocol.Messages.RequestKeyFrame;
import com.gamescrcpy.protocol.Messages.RequestScreenshot;
import com.gamescrcpy.protocol.Messages.Scroll;
import com.gamescrcpy.protocol.Messages.SetClipboard;
import com.gamescrcpy.protocol.Messages.SetDisplayPower;
import com.gamescrcpy.protocol.Messages.SetOrientation;
import com.gamescrcpy.protocol.Messages.SetMaxFps;
import com.gamescrcpy.protocol.Messages.SetShowTouches;
import com.gamescrcpy.protocol.Messages.SetVideoBitrate;
import com.gamescrcpy.protocol.Messages.SetVideoPaused;
import com.gamescrcpy.protocol.Messages.StartApp;
import com.gamescrcpy.protocol.Messages.SubscribeStatus;
import com.gamescrcpy.protocol.Messages.SystemPanel;
import com.gamescrcpy.protocol.Messages.Text;
import com.gamescrcpy.protocol.Messages.TouchFrame;
import com.gamescrcpy.protocol.Messages.UhidCreate;
import com.gamescrcpy.protocol.Messages.UhidDestroy;
import com.gamescrcpy.protocol.Messages.UhidFeature;
import com.gamescrcpy.protocol.Messages.UhidInput;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.audio.AudioController;
import com.gamescrcpy.server.device.AppService;
import com.gamescrcpy.server.device.BulkRequests;
import com.gamescrcpy.server.device.ClipboardSync;
import com.gamescrcpy.server.device.DeviceStatusSampler;
import com.gamescrcpy.server.device.ForegroundTracker;
import com.gamescrcpy.server.device.OrientationLock;
import com.gamescrcpy.server.device.ScreenPower;
import com.gamescrcpy.server.device.ScreenshotService;
import com.gamescrcpy.server.device.ShowTouches;
import com.gamescrcpy.server.device.SystemPanels;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.input.InputController;
import com.gamescrcpy.server.input.TextInjector;
import com.gamescrcpy.server.input.TouchGeometry;
import com.gamescrcpy.server.session.SessionServer;
import com.gamescrcpy.server.transport.FrameEndpoint;
import com.gamescrcpy.server.transport.MediaEndpoint;
import com.gamescrcpy.server.uhid.DevUhid;
import com.gamescrcpy.server.uhid.UhidManager;
import com.gamescrcpy.server.video.VideoController;
import com.gamescrcpy.server.wrappers.Apps;
import com.gamescrcpy.server.wrappers.BatteryBroadcast;
import com.gamescrcpy.server.wrappers.Clipboard;
import com.gamescrcpy.server.wrappers.DisplayPower;
import com.gamescrcpy.server.wrappers.RotationLock;
import com.gamescrcpy.server.wrappers.DisplayWatcher;
import com.gamescrcpy.server.wrappers.Displays;
import com.gamescrcpy.server.wrappers.ForegroundApps;
import com.gamescrcpy.server.wrappers.InputInjector;
import com.gamescrcpy.server.wrappers.PlatformException;
import com.gamescrcpy.server.wrappers.PowerState;
import com.gamescrcpy.server.wrappers.Screenshots;
import com.gamescrcpy.server.wrappers.StatusBar;
import com.gamescrcpy.server.wrappers.SystemSettings;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

final class ServerSessionHandler implements SessionServer.SessionHandler {

    private static final String SHOW_TOUCHES = "show_touches";

    private final VideoController video;
    private final AudioController audio = new AudioController(
            (code, message) -> notice(NoticeLevel.WARNING, code, message));
    private final InputController input;
    private final ClipboardSync clipboard;
    private final ScreenPower screen;
    private final OrientationLock orientation;
    private final ShowTouches touches;
    private final ForegroundTracker foregroundState = new ForegroundTracker();
    private final ForegroundApps foreground;
    private final AppService apps;
    private final UhidManager uhid;
    private final BulkRequests bulkRequests = new BulkRequests();
    private final ScreenshotService screenshots;
    private final DeviceStatusSampler status;
    private final SystemPanels panels;
    private final Map<Integer, DisplaySnapshot> reported = new HashMap<>();
    private DisplayWatcher watcher;
    private volatile FrameEndpoint control;

    ServerSessionHandler(VideoController video) {
        this.video = video;
        Clipboard clipboard = null;
        try {
            clipboard = Clipboard.create();
        } catch (PlatformException e) {
            Log.w("no clipboard: text beyond the keyboard cannot be typed, nor the clipboard shared", e);
        }
        this.clipboard = clipboard == null ? null : new ClipboardSync(clipboard, new ClipboardPeer());
        InputController created = null;
        try {
            final InputInjector injector = InputInjector.create(reason -> notice(ErrorCode.INJECTION_DENIED,
                    "input injection is not permitted (INJECT_EVENTS): " + reason));
            final TextInjector text = new TextInjector(injector, clipboard,
                    reason -> notice(ErrorCode.CLIPBOARD_DENIED, reason));
            created = new InputController(injector, text, this::touchGeometry, PowerState::interactive);
        } catch (PlatformException e) {
            Log.w("input will not be injected", e);
        }
        this.input = created;
        ScreenPower screenPower = null;
        try {
            screenPower = new ScreenPower(DisplayPower.create());
        } catch (PlatformException e) {
            Log.w("the screen cannot be turned off while mirroring", e);
        }
        this.screen = screenPower;
        OrientationLock orientationLock = null;
        try {
            orientationLock = new OrientationLock(RotationLock.create());
        } catch (PlatformException e) {
            Log.w("the orientation cannot be locked", e);
        }
        this.orientation = orientationLock;
        ShowTouches showTouches = null;
        try {
            final SystemSettings settings = SystemSettings.create();
            settings.getSystem(SHOW_TOUCHES);
            showTouches = new ShowTouches(touchSetting(settings));
        } catch (PlatformException e) {
            Log.w("touches cannot be shown on the phone, so latency cannot be measured with them", e);
        }
        this.touches = showTouches;
        ForegroundApps foregroundApps = null;
        try {
            foregroundApps = ForegroundApps.start(this::foregroundChanged, this::foregroundDisplay);
        } catch (PlatformException e) {
            Log.w("the foreground app will not be reported", e);
        }
        this.foreground = foregroundApps;
        AppService appService = null;
        try {
            appService = new AppService(Apps.create(), video::videoDisplay, this::notice, bulkRequests);
        } catch (PlatformException e) {
            Log.w("apps will not be listed or started", e);
        }
        this.apps = appService;
        this.uhid = UhidManager.available(DevUhid::open) ? UhidManager.onPhone(DevUhid::open, this::send) : null;
        ScreenshotService screenshotService = null;
        try {
            screenshotService = new ScreenshotService(Screenshots.create(video::showsIndependent), bulkRequests);
        } catch (PlatformException e) {
            Log.w("lossless screenshots cannot be taken", e);
        }
        this.screenshots = screenshotService;
        this.status = new DeviceStatusSampler(batteryBroadcast(), DeviceStatusSampler::readProcStat, this::send);
        SystemPanels.Platform statusBar;
        try {
            statusBar = StatusBar.create();
        } catch (PlatformException e) {
            Log.w("the notification shade cannot be pulled down from the computer", e);
            statusBar = SystemPanels.unavailable(e.getMessage());
        }
        this.panels = new SystemPanels(statusBar, this::notice);
    }

    long capabilities() {
        long capabilities = 0;
        if (clipboard != null) {
            capabilities |= Constants.CAPABILITY_CLIPBOARD;
        }
        if (input != null) {
            capabilities |= Constants.CAPABILITY_TEXT;
        }
        if (uhid != null) {
            capabilities |= Constants.CAPABILITY_UHID | Constants.CAPABILITY_UHID_GAMEPAD;
        }
        if (screen != null) {
            capabilities |= Constants.CAPABILITY_DISPLAY_POWER;
        }
        if (orientation != null) {
            capabilities |= Constants.CAPABILITY_ROTATION;
        }
        if (foreground != null) {
            capabilities |= Constants.CAPABILITY_FOREGROUND_APP;
        }
        if (touches != null) {
            capabilities |= Constants.CAPABILITY_SHOW_TOUCHES;
        }
        if (apps != null) {
            capabilities |= Constants.CAPABILITY_APPS;
            if (input != null && video.supportsIndependentDisplays()) {
                capabilities |= Constants.CAPABILITY_INDEPENDENT_DISPLAY;
            }
        }
        if (screenshots != null) {
            capabilities |= Constants.CAPABILITY_SCREENSHOT;
        }
        capabilities |= Constants.CAPABILITY_DEVICE_STATUS;
        return capabilities;
    }

    @Override
    public void controlConnected(FrameEndpoint newControl) {
        control = newControl;
        video.controlConnected(newControl);
        if (clipboard != null) {
            clipboard.controlConnected();
        }
        synchronized (this) {
            reported.clear();
            try {
                for (DisplaySnapshot display : Displays.get().list()) {
                    reported.put(display.displayId(), display);
                }
            } catch (PlatformException e) {
                Log.w("cannot list displays", e);
            }
            if (watcher == null) {
                try {
                    watcher = DisplayWatcher.start(this::displayChanged);
                } catch (PlatformException e) {
                    Log.w("rotation and display changes will not be followed", e);
                }
            }
        }
        if (foreground != null) {
            synchronized (foregroundState) {
                send(foregroundState.message());
            }
        }
    }

    @Override
    public void channelOpened(ChannelId channel, MediaEndpoint media) {
        if (channel == ChannelId.VIDEO) {
            video.videoChannelOpened(media);
        } else if (channel == ChannelId.AUDIO) {
            audio.channelOpened(media);
        }
    }

    @Override
    public void bulkOpened(FrameEndpoint bulk) {
        bulkRequests.bulkOpened(bulk);
    }

    @Override
    public void handle(Message message, FrameEndpoint channel) {
        if (message instanceof TouchFrame touch) {
            if (input != null) {
                input.touch(touch);
            }
        } else if (message instanceof Key key) {
            if (input != null) {
                input.key(key);
            }
        } else if (message instanceof Text text) {
            if (input != null) {
                input.text(text);
            }
        } else if (message instanceof Scroll scroll) {
            if (input != null) {
                input.scroll(scroll);
            }
        } else if (message instanceof BackOrScreenOn back) {
            if (input != null) {
                input.backOrScreenOn(back);
            }
        } else if (message instanceof UhidCreate create) {
            if (uhid != null) {
                uhid.create(create);
            }
        } else if (message instanceof UhidInput report) {
            if (uhid != null) {
                uhid.input(report);
            }
        } else if (message instanceof UhidDestroy destroy) {
            if (uhid != null) {
                uhid.destroy(destroy);
            }
        } else if (message instanceof UhidFeature feature) {
            if (uhid != null) {
                uhid.feature(feature);
            }
        } else if (message instanceof ConfigureVideo configure) {
            video.configure(configure);
            if (foreground != null) {
                foreground.refresh();
            }
        } else if (message instanceof SetVideoBitrate bitrate) {
            video.setBitrate(bitrate.bitrate);
        } else if (message instanceof SetMaxFps maxFps) {
            video.setMaxFps(maxFps.maxFps);
        } else if (message instanceof RequestKeyFrame) {
            video.requestKeyFrame();
        } else if (message instanceof SetVideoPaused paused) {
            video.setPaused(paused.paused);
        } else if (message instanceof ConfigureAudio configure) {
            audio.configure(configure.enabled, configure.bitrate);
        } else if (message instanceof SetClipboard set) {
            if (clipboard != null) {
                try {
                    clipboard.set(set);
                } catch (RuntimeException e) {
                    notice(ErrorCode.CLIPBOARD_DENIED, "the clipboard refused the computer's text: " + e.getMessage());
                }
            }
        } else if (message instanceof GetClipboard get) {
            if (clipboard != null) {
                clipboard.get(get);
            }
        } else if (message instanceof SetDisplayPower power) {
            if (screen != null) {
                try {
                    screen.set(power.on);
                } catch (PlatformException e) {
                    notice(ErrorCode.INTERNAL_ERROR, e.getMessage());
                }
            }
        } else if (message instanceof SetOrientation orient) {
            if (orientation != null) {
                try {
                    orientation.set(orient.mode.wireValue());
                } catch (PlatformException e) {
                    notice(ErrorCode.INTERNAL_ERROR, e.getMessage());
                }
            }
        } else if (message instanceof ReceiverReport report) {
            video.receiverReport(report);
        } else if (message instanceof SetShowTouches show) {
            if (touches != null) {
                try {
                    touches.set(show.on);
                } catch (PlatformException e) {
                    notice(ErrorCode.PERMISSION_DENIED, "the phone refused to show touches: " + e.getMessage());
                }
            }
        } else if (message instanceof RequestAppList request) {
            if (apps != null) {
                apps.list(request);
            }
        } else if (message instanceof RequestAppIcon request) {
            if (apps != null) {
                apps.icon(request);
            }
        } else if (message instanceof RequestScreenshot request) {
            if (screenshots != null) {
                screenshots.capture(request);
            }
        } else if (message instanceof StartApp start) {
            if (apps != null) {
                apps.start(start);
            }
        } else if (message instanceof SubscribeStatus subscribe) {
            status.subscribe(subscribe.on, subscribe.periodMs);
        } else if (message instanceof SystemPanel panel) {
            panels.handle(panel);
        } else {
            Log.d(message.type().specName() + " is not supported yet");
        }
    }

    @Override
    public void connectionLost() {
        if (input != null) {
            input.releaseAll();
        }
        if (uhid != null) {
            uhid.connectionLost();
        }
        if (touches != null) {
            touches.restore();
        }
        status.stop();
        control = null;
        video.connectionLost();
        audio.connectionLost();
        if (clipboard != null) {
            clipboard.connectionLost();
        }
        bulkRequests.connectionLost();
    }

    private final class ClipboardPeer implements ClipboardSync.Peer {
        @Override
        public void send(Message message) {
            final FrameEndpoint target = control;
            if (target == null) {
                return;
            }
            try {
                target.write(message);
            } catch (IOException e) {
            }
        }

        @Override
        public void pressKey(int keycode) {
            if (input == null) {
                return;
            }
            final Key key = new Key();
            key.keycode = keycode;
            key.action = KeyAction.DOWN;
            input.key(key);
            key.action = KeyAction.UP;
            input.key(key);
        }

        @Override
        public void refused(String reason) {
            notice(NoticeLevel.WARNING, ErrorCode.CLIPBOARD_DENIED, reason);
        }
    }

    private TouchGeometry touchGeometry() {
        final VideoController.Current current = video.current();
        if (current == null) {
            return null;
        }
        final VideoSessionInfo info = current.info();
        return new TouchGeometry(current.number(), (int) info.displayId, info.displayWidth, info.displayHeight);
    }

    private void notice(ErrorCode code, String message) {
        notice(NoticeLevel.ERROR, code, message);
    }

    private void notice(NoticeLevel level, ErrorCode code, String message) {
        Log.w(message);
        final FrameEndpoint target = control;
        if (target == null) {
            return;
        }
        final Notice notice = new Notice();
        notice.level = level;
        notice.code = code;
        notice.message = message.length() > 1000 ? message.substring(0, 1000) : message;
        try {
            target.write(notice);
        } catch (IOException e) {
        }
    }

    void shutdown() {
        connectionLost();
        restoreDevice();
        synchronized (this) {
            if (watcher != null) {
                watcher.close();
                watcher = null;
            }
        }
        if (foreground != null) {
            foreground.close();
        }
        if (apps != null) {
            apps.close();
        }
        if (screenshots != null) {
            screenshots.close();
        }
        status.close();
        bulkRequests.close();
        video.close();
    }

    private int foregroundDisplay() {
        return Math.max(0, video.videoDisplay());
    }

    private void foregroundChanged(String packageName, String activity, boolean game) {
        synchronized (foregroundState) {
            if (foregroundState.update(packageName, activity, game)) {
                send(foregroundState.message());
            }
        }
    }

    private void send(Message message) {
        final FrameEndpoint target = control;
        if (target == null) {
            return;
        }
        try {
            target.write(message);
        } catch (IOException e) {
        }
    }

    void restoreDevice() {
        if (screen != null) {
            screen.restore();
        }
        if (orientation != null) {
            orientation.restore();
        }
        if (touches != null) {
            touches.restore();
        }
    }

    private static DeviceStatusSampler.Battery batteryBroadcast() {
        try {
            return BatteryBroadcast.create();
        } catch (PlatformException e) {
            Log.w("the battery level and temperature will not be reported", e);
            return () -> null;
        }
    }

    private static ShowTouches.Store touchSetting(SystemSettings settings) {
        return new ShowTouches.Store() {
            @Override
            public String get() throws PlatformException {
                return settings.getSystem(SHOW_TOUCHES);
            }

            @Override
            public void put(String value) throws PlatformException {
                settings.putSystem(SHOW_TOUCHES, value);
            }
        };
    }

    private void displayChanged(int displayId) {
        final boolean independent = video.showsIndependent(displayId);
        final DisplaySnapshot snapshot;
        try {
            snapshot = independent ? Displays.get().snapshotOfAny(displayId) : Displays.get().snapshot(displayId);
        } catch (PlatformException e) {
            Log.w("cannot read display " + displayId, e);
            return;
        }
        if (snapshot == null) {
            return;
        }
        video.displayChanged(snapshot);
        if (independent) {
            return;
        }
        synchronized (this) {
            if (snapshot.sameReport(reported.get(displayId))) {
                return;
            }
            reported.put(displayId, snapshot);
        }
        final FrameEndpoint target = control;
        if (target != null) {
            final DisplayChanged changed = new DisplayChanged();
            snapshot.fill(changed.display);
            try {
                target.write(changed);
            } catch (IOException e) {
            }
        }
    }
}
