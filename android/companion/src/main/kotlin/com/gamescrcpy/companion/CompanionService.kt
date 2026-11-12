package com.gamescrcpy.companion

import android.Manifest
import android.app.AppOpsManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.hardware.display.DisplayManager
import android.net.ConnectivityManager
import android.net.Network
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.Settings
import java.io.IOException
import kotlin.concurrent.thread

class CompanionService : Service(), ProjectionHost {
    private val main = Handler(Looper.getMainLooper())
    private var capture: ScreenCapture? = null
    private var projectingNow = false
    private val permissionWatcher = AppOpsManager.OnOpChangedListener { _, _ -> main.post { refreshOverlay() } }
    private val sink = CursorSink { sample -> overlay?.update(sample) }
    private val hints = object : KeyHintSink {
        override fun layout(items: List<HintItem>) {
            main.post {
                hintItems = items
                overlay?.showHints(items)
            }
        }

        override fun state(state: HintState) {
            main.post {
                hintState = state
                overlay?.hintState(state)
            }
        }
    }
    private var server: CompanionServer? = null
    private var lanServer: CompanionServer? = null
    private var networkWatcher: ConnectivityManager.NetworkCallback? = null
    private var hintItems: List<HintItem> = emptyList()
    private var hintState = HintState.HIDDEN

    @Volatile
    private var connector: LanConnector? = null

    @Volatile
    private var overlay: CursorOverlay? = null
    private var overlayInfo = OverlayInfo.NONE
    private var lastStartId = 0
    private var foreground = false
    private var topHint: TopHint? = null
    private val sync: SettingsSync by lazy { SettingsSync.of(this) }
    private val options: PhoneOptions by lazy { PhoneOptions.of(this) }
    private var autoConnect = true
    private val syncListener = SettingsSync.Listener { main.post { syncChanged() } }
    private val optionsListener = PhoneOptions.Listener { main.post { applyOptions() } }
    private val boxWatcher: (InputBox) -> Unit = { main.post { refreshTopHint() } }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val channel = NotificationChannel(
            CHANNEL,
            getString(R.string.notification_channel),
            NotificationManager.IMPORTANCE_LOW,
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        getSystemService(AppOpsManager::class.java)
            .startWatchingMode(AppOpsManager.OPSTR_SYSTEM_ALERT_WINDOW, packageName, permissionWatcher)
        capture = ScreenCapture(this, this)
        autoConnect = sync.values.autoConnect
        sync.addListener(syncListener)
        options.addListener(optionsListener)
        ImeBridge.watch(boxWatcher)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        when (intent?.action) {
            ACTION_STOP -> {
                stopByUser()
                return START_NOT_STICKY
            }
            ACTION_PAIR -> {
                goForeground()
                val link = QrLinks.parse(intent?.getStringExtra(EXTRA_LINK).orEmpty())
                if (link == null) {
                    stopIfIdle()
                } else {
                    startLan(link)
                }
                return START_NOT_STICKY
            }
            ACTION_CONNECT -> {
                goForeground()
                startLan(null)
                return START_NOT_STICKY
            }
        }
        goForeground()
        val token = Tokens.parse(intent?.getStringExtra(EXTRA_TOKEN))
        if (token == null) {
            if (server == null && lanServer == null) {
                stopSelf(startId)
            }
            return START_NOT_STICKY
        }
        val running = server?.takeUnless { it.ended } ?: open()
        if (running == null) {
            stopIfIdle()
            return START_NOT_STICKY
        }
        running.setToken(token)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        sync.removeListener(syncListener)
        options.removeListener(optionsListener)
        ImeBridge.unwatch(boxWatcher)
        ImeBridge.hostTop(false)
        topHint?.hide()
        topHint = null
        foreground = false
        getSystemService(AppOpsManager::class.java).stopWatchingMode(permissionWatcher)
        networkWatcher?.let { callback ->
            runCatching { getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(callback) }
        }
        networkWatcher = null
        connector?.close()
        connector = null
        server?.close()
        server = null
        lanServer?.close()
        lanServer = null
        capture?.close()
        capture = null
        overlay?.detach()
        overlay = null
        main.removeCallbacksAndMessages(null)
        CompanionState.update(listening = false, connected = false)
        CompanionState.updateLan(LanState.IDLE, connected = false)
        super.onDestroy()
    }

    private fun open(): CompanionServer? {
        server?.close()
        server = null
        val listener = try {
            LocalListener(SOCKET)
        } catch (e: IOException) {
            CompanionState.update(listening = false, connected = false)
            return null
        }
        val events = ServerEvents()
        val created = CompanionServer(
            listener,
            sink,
            events,
            CompanionServer.Timing(),
            hints,
            screenshots = capture ?: ScreenshotPort.NONE,
            sync = sync,
        )
        events.owner = created
        created.overlayChanged(overlayInfo)
        server = created
        created.start()
        CompanionState.update(listening = true, connected = false)
        refreshOverlay()
        return created
    }

    private fun startLan(link: QrLink?) {
        val pairings = PairingStore(this)
        if (link == null && pairings.all().isEmpty()) {
            CompanionState.updateLan(LanState.IDLE, connected = false)
            stopIfIdle()
            return
        }
        if (lanServer == null) {
            val queue = QueueListener()
            val events = LanEvents()
            val created = CompanionServer(
                queue,
                sink,
                events,
                CompanionServer.Timing(),
                hints,
                persistent = true,
                screenshots = capture ?: ScreenshotPort.NONE,
                sync = sync,
            )
            events.owner = created
            created.overlayChanged(overlayInfo)
            created.start()
            lanServer = created
            connector = LanConnector(
                pairings,
                NsdDiscovery(this),
                { connection -> queue.offer(connection) },
                events,
                keepTrying = { sync.values.autoConnect },
            )
            watchNetwork()
            refreshOverlay()
        }
        connector?.start(link)
    }

    private fun syncChanged() {
        val wanted = sync.values.autoConnect
        if (wanted && !autoConnect) {
            connector?.start(null)
        }
        autoConnect = wanted
        refreshNotification()
    }

    private fun watchNetwork() {
        if (networkWatcher != null) {
            return
        }
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                connector?.nudge()
            }
        }
        runCatching { getSystemService(ConnectivityManager::class.java).registerDefaultNetworkCallback(callback) }
            .onSuccess { networkWatcher = callback }
    }

    private fun stopIfIdle() {
        val local = server?.takeUnless { it.ended }
        val remote = lanServer?.takeUnless { it.ended }
        if (local == null && remote == null) {
            stopSelfResult(lastStartId)
        }
    }

    private fun stopByUser() {
        val local = server?.takeUnless { it.ended }
        val remote = lanServer?.takeUnless { it.ended }
        connector?.close()
        if (local == null && remote == null) {
            stopSelf()
            return
        }
        thread(name = "companion-stop", isDaemon = true) {
            local?.stop()
            remote?.stop()
            main.post { stopSelf() }
        }
    }

    private fun refreshOverlay() {
        if (server == null && lanServer == null) {
            return
        }
        if (!Settings.canDrawOverlays(this)) {
            overlay?.detach()
            overlay = null
            overlayChanged(OverlayInfo(granted = false, width = 0, height = 0, rotation = rotation()))
            refreshTopHint()
            return
        }
        if (overlay != null) {
            return
        }
        val created = try {
            CursorOverlay(this, ::overlayChanged)
        } catch (e: RuntimeException) {
            null
        }
        if (created != null && created.attach()) {
            created.showHints(hintItems)
            created.hintState(hintState)
            created.cursorLook(options.cursorSize, options.cursorStyle)
            overlay = created
            overlayChanged(OverlayInfo(granted = true, width = 0, height = 0, rotation = rotation()))
        } else {
            overlayChanged(OverlayInfo(granted = false, width = 0, height = 0, rotation = rotation()))
        }
        refreshTopHint()
    }

    private fun applyOptions() {
        overlay?.cursorLook(options.cursorSize, options.cursorStyle)
        refreshTopHint()
    }

    private fun refreshTopHint() {
        val host = options.imeBar == ImeBar.TOP && overlay != null
        ImeBridge.hostTop(host)
        if (host && ImeBridge.box.shown) {
            val hint = topHint ?: TopHint(this) { ImeBridge.switchBack() }.also { topHint = it }
            hint.show()
        } else {
            topHint?.hide()
        }
    }

    private fun refreshNotification() {
        if (foreground) {
            goForeground()
        }
    }

    private fun overlayChanged(info: OverlayInfo) {
        if (info == overlayInfo) {
            return
        }
        overlayInfo = info
        server?.overlayChanged(info)
        lanServer?.overlayChanged(info)
    }

    private fun rotation(): Int = CursorOverlay.rotationOf(getSystemService(DisplayManager::class.java))

    override fun projecting(on: Boolean): Boolean =
        try {
            goForeground(on)
            projectingNow = on
            true
        } catch (e: RuntimeException) {
            false
        }

    override fun capturePrompt(shown: Boolean) {
        val notifications = getSystemService(NotificationManager::class.java)
        if (!shown) {
            notifications.cancel(CAPTURE_NOTIFICATION_ID)
            return
        }
        if (Settings.canDrawOverlays(this)) {
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            return
        }
        val allow = PendingIntent.getActivity(
            this,
            CAPTURE_NOTIFICATION_ID,
            Intent(this, CaptureActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val notification = Notification.Builder(this, CHANNEL)
            .setSmallIcon(Icon.createWithResource(this, R.drawable.ic_notification))
            .setContentTitle(getString(R.string.capture_title))
            .setContentText(getString(R.string.capture_text))
            .setContentIntent(allow)
            .setAutoCancel(true)
            .build()
        notifications.notify(CAPTURE_NOTIFICATION_ID, notification)
    }

    @Suppress("DEPRECATION")
    private fun goForeground(projecting: Boolean = projectingNow) {
        val icon = Icon.createWithResource(this, R.drawable.ic_notification)
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val stop = PendingIntent.getService(
            this,
            0,
            Intent(this, CompanionService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val computer = sync.connected
        val connected = CompanionState.connected
        val lan = CompanionState.lan
        val title = when {
            connected && computer != null -> getString(R.string.notification_connected_to, Labels.name(this, computer))
            connected -> getString(R.string.notification_connected)
            lan == LanState.SEARCHING || lan == LanState.PAIRING -> getString(R.string.notification_searching)
            else -> getString(R.string.notification_waiting)
        }
        val builder = Notification.Builder(this, CHANNEL)
            .setSmallIcon(icon)
            .setColor(Look.OPAQUE or (computer?.accent ?: Look.EMBER))
            .setContentTitle(title)
            .setContentIntent(open)
            .setOngoing(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .addAction(Notification.Action.Builder(icon, getString(R.string.notification_stop), stop).build())
        if (connected) {
            val overLan = CompanionState.lanConnected && !CompanionState.localConnected
            builder.setContentText(getString(Labels.link(computer, overLan)))
                .setWhen(CompanionState.connectedAt)
                .setShowWhen(true)
                .setUsesChronometer(true)
        } else {
            builder.setShowWhen(false)
        }
        val notification = builder.build()
        foreground = true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            val type = if (projecting) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
            }
            startForeground(NOTIFICATION_ID, notification, type)
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val type = if (projecting) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            } else {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_NONE
            }
            startForeground(NOTIFICATION_ID, notification, type)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private inner class ServerEvents : CompanionServer.Events {
        var owner: CompanionServer? = null

        override fun connectionChanged(connected: Boolean) {
            main.post {
                if (owner === server) {
                    CompanionState.update(listening = true, connected = connected)
                    refreshNotification()
                }
            }
        }

        override fun ended(ending: CompanionServer.Ending) {
            main.post {
                if (owner !== server) {
                    return@post
                }
                if (lanServer == null) {
                    stopSelfResult(lastStartId)
                } else {
                    CompanionState.update(listening = false, connected = false)
                }
            }
        }
    }

    private inner class LanEvents : CompanionServer.Events, LanConnector.Events {
        var owner: CompanionServer? = null

        override fun connectionChanged(connected: Boolean) {
            main.post {
                if (owner === lanServer) {
                    CompanionState.updateLan(CompanionState.lan, connected)
                    refreshNotification()
                }
            }
        }

        override fun ended(ending: CompanionServer.Ending) = Unit

        override fun state(state: LanState) {
            main.post {
                if (owner === lanServer) {
                    CompanionState.updateLan(state, CompanionState.lanConnected)
                    refreshNotification()
                }
            }
        }

        override fun paired(pairing: Pairing) = Unit
    }

    companion object {
        const val EXTRA_TOKEN = "token"
        const val EXTRA_LINK = "link"
        const val ACTION_PAIR = "com.gamescrcpy.companion.action.PAIR"
        const val ACTION_CONNECT = "com.gamescrcpy.companion.action.CONNECT"
        const val ACTION_STOP = "com.gamescrcpy.companion.action.STOP"
        private const val SOCKET = "gamescrcpy-companion"
        private const val CHANNEL = "companion"
        private const val NOTIFICATION_ID = 1
        private const val CAPTURE_NOTIFICATION_ID = 2
    }
}
