package com.gamescrcpy.companion

import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.LinearLayout

class MainActivity : Activity(), Shell {
    override val activity: Activity
        get() = this
    override lateinit var ui: Ui
    override val sync: SettingsSync by lazy { SettingsSync.of(this) }
    override val options: PhoneOptions by lazy { PhoneOptions.of(this) }

    private lateinit var host: FrameLayout
    private lateinit var nav: NavBar
    private var pages: List<Page> = emptyList()
    private var current = Page.HOME
    private var accent = 0
    private var top = 0
    private val asked = HashSet<String>()
    private val stateListener = CompanionState.Listener { runOnUiThread { refresh() } }
    private val syncListener = SettingsSync.Listener { runOnUiThread { refresh() } }
    private val optionsListener = PhoneOptions.Listener { runOnUiThread { refresh() } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        current = savedInstanceState?.getInt(PAGE, Page.HOME)?.coerceIn(Page.HOME, Page.ABOUT) ?: Page.HOME
        build()
    }

    override fun onResume() {
        super.onResume()
        CompanionState.addListener(stateListener)
        sync.addListener(syncListener)
        options.addListener(optionsListener)
        refresh()
    }

    override fun onPause() {
        CompanionState.removeListener(stateListener)
        sync.removeListener(syncListener)
        options.removeListener(optionsListener)
        super.onPause()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(PAGE, current)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            refresh()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        refresh()
    }

    override fun open(page: Int) {
        if (page == current) {
            return
        }
        current = page
        nav.select(page)
        show(animate = true)
    }

    override fun scan() {
        startActivity(Intent(this, ScanActivity::class.java))
    }

    override fun connectNow() {
        startForegroundService(Intent(this, CompanionService::class.java).setAction(CompanionService.ACTION_CONNECT))
    }

    override fun stopCompanion() {
        startService(Intent(this, CompanionService::class.java).setAction(CompanionService.ACTION_STOP))
    }

    override fun ask(permission: String): Boolean {
        if (checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED || !asked.add(permission)) {
            return false
        }
        requestPermissions(arrayOf(permission), REQUEST_PERMISSION)
        return true
    }

    private fun build() {
        accent = wantedAccent()
        ui = Ui(this, Look.palette(this, accent))
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(ui.palette.bgRaised)
        }
        host = FrameLayout(this)
        root.addView(host, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        nav = NavBar(ui) { open(it) }
        nav.add(NavBar.Entry(R.drawable.ic_nav_home, R.string.nav_home))
        nav.add(NavBar.Entry(R.drawable.ic_nav_computers, R.string.nav_computers))
        nav.add(NavBar.Entry(R.drawable.ic_nav_features, R.string.nav_features))
        nav.add(NavBar.Entry(R.drawable.ic_nav_permissions, R.string.nav_permissions))
        nav.add(NavBar.Entry(R.drawable.ic_nav_about, R.string.nav_about))
        root.addView(nav.view, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT))
        pages = listOf(HomePage(this), ComputersPage(this), FeaturesPage(this), PermissionsPage(this), AboutPage(this))
        root.setOnApplyWindowInsetsListener { view, insets ->
            val bars = barsOf(insets)
            view.setPadding(bars[0], 0, bars[2], 0)
            top = bars[1]
            pages.forEach { it.inset(top) }
            nav.view.setPadding(0, 0, 0, bars[3])
            insets
        }
        setContentView(root)
        pages.forEach { it.inset(top) }
        nav.select(current)
        show(animate = false)
    }

    private fun show(animate: Boolean) {
        val page = pages[current]
        val incoming = page.view
        for (index in 0 until host.childCount) {
            host.getChildAt(index).animate().cancel()
        }
        val outgoing = (0 until host.childCount).map { host.getChildAt(it) }.filter { it !== incoming }
        if (incoming.parent == null) {
            host.addView(incoming, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT))
        }
        page.refresh()
        incoming.visibility = View.VISIBLE
        if (!animate || Look.reducedMotion()) {
            outgoing.forEach { host.removeView(it) }
            incoming.alpha = 1f
            incoming.translationY = 0f
            return
        }
        incoming.bringToFront()
        incoming.alpha = 0f
        incoming.translationY = Look.dp(this, Look.PAGE_SHIFT)
        incoming.animate().alpha(1f).translationY(0f).setDuration(Look.BASE).setInterpolator(Look.standard).start()
        outgoing.forEach { view ->
            view.animate().alpha(0f).setDuration(Look.FAST).setInterpolator(Look.exit)
                .withEndAction { host.removeView(view) }.start()
        }
    }

    private fun refresh() {
        if (!::ui.isInitialized) {
            return
        }
        if (wantedAccent() != accent) {
            build()
            return
        }
        pages.getOrNull(current)?.refresh()
    }

    private fun wantedAccent(): Int = sync.connected?.accent ?: (Look.EMBER and Look.RGB)

    private fun barsOf(insets: WindowInsets): IntArray {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            return intArrayOf(bars.left, bars.top, bars.right, bars.bottom)
        }
        return legacyBarsOf(insets)
    }

    @Suppress("DEPRECATION")
    private fun legacyBarsOf(insets: WindowInsets): IntArray = intArrayOf(
        insets.systemWindowInsetLeft,
        insets.systemWindowInsetTop,
        insets.systemWindowInsetRight,
        insets.systemWindowInsetBottom,
    )

    private companion object {
        const val PAGE = "page"
        const val REQUEST_PERMISSION = 7
    }
}
