package com.gamescrcpy.companion

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.TypedValue
import android.view.View
import android.view.WindowInsets
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var permission: TextView
    private lateinit var openSettings: Button
    private lateinit var connection: TextView
    private lateinit var lan: TextView
    private lateinit var connectNow: Button
    private lateinit var ime: TextView
    private val listener = CompanionState.Listener { runOnUiThread { refresh() } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val padding = dp(PADDING_DP)
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
        }
        content.addView(text(R.string.app_name, TITLE_SP))
        content.addView(text(R.string.overlay_title, HEADING_SP).apply { setPadding(0, padding, 0, 0) })
        permission = text(R.string.overlay_missing, BODY_SP)
        content.addView(permission)
        openSettings = button(R.string.overlay_open) { openOverlaySettings() }
        content.addView(openSettings)
        content.addView(text(R.string.connection_title, HEADING_SP).apply { setPadding(0, padding, 0, 0) })
        connection = text(R.string.connection_none, BODY_SP)
        content.addView(connection)
        content.addView(text(R.string.lan_title, HEADING_SP).apply { setPadding(0, padding, 0, 0) })
        lan = text(R.string.lan_none, BODY_SP)
        content.addView(lan)
        content.addView(button(R.string.lan_scan) { startActivity(Intent(this, ScanActivity::class.java)) })
        connectNow = button(R.string.lan_connect) {
            startForegroundService(Intent(this, CompanionService::class.java).setAction(CompanionService.ACTION_CONNECT))
        }
        content.addView(connectNow)
        content.addView(text(R.string.ime_title, HEADING_SP).apply { setPadding(0, padding, 0, 0) })
        ime = text(R.string.ime_disabled, BODY_SP)
        content.addView(ime)
        content.addView(button(R.string.ime_enable) { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) })
        content.addView(button(R.string.ime_choose) { inputMethods().showInputMethodPicker() })
        content.setOnApplyWindowInsetsListener { view, insets ->
            val bars = barsOf(insets)
            view.setPadding(padding + bars[0], padding + bars[1], padding + bars[2], padding + bars[3])
            insets
        }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    override fun onResume() {
        super.onResume()
        CompanionState.addListener(listener)
        refresh()
    }

    override fun onPause() {
        CompanionState.removeListener(listener)
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            refresh()
        }
    }

    private fun refresh() {
        val granted = Settings.canDrawOverlays(this)
        permission.setText(if (granted) R.string.overlay_granted else R.string.overlay_missing)
        openSettings.visibility = if (granted) View.GONE else View.VISIBLE
        connection.setText(
            when {
                CompanionState.localConnected -> R.string.connection_connected
                CompanionState.listening -> R.string.connection_waiting
                else -> R.string.connection_none
            },
        )
        val paired = PairingStore(this).all().isNotEmpty()
        lan.setText(
            when {
                CompanionState.lanConnected -> R.string.lan_connected
                CompanionState.lan == LanState.PAIRING || CompanionState.lan == LanState.SEARCHING -> R.string.lan_searching
                CompanionState.lan == LanState.FAILED -> R.string.lan_failed
                paired -> R.string.lan_paired
                else -> R.string.lan_none
            },
        )
        connectNow.visibility = if (paired && !CompanionState.lanConnected) View.VISIBLE else View.GONE
        ime.setText(
            when {
                imeChosen() -> R.string.ime_ready
                imeEnabled() -> R.string.ime_enabled
                else -> R.string.ime_disabled
            },
        )
    }

    private fun imeEnabled(): Boolean = inputMethods().enabledInputMethodList.any { it.packageName == packageName }

    private fun imeChosen(): Boolean =
        Settings.Secure.getString(contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)?.startsWith("$packageName/") == true

    private fun inputMethods(): InputMethodManager = getSystemService(InputMethodManager::class.java)

    private fun openOverlaySettings() {
        startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
    }

    private fun text(resource: Int, sizeSp: Float): TextView = TextView(this).apply {
        setText(resource)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
    }

    private fun button(resource: Int, action: () -> Unit): Button = Button(this).apply {
        setText(resource)
        setOnClickListener { action() }
    }

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics).toInt()

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
        const val PADDING_DP = 24f
        const val TITLE_SP = 22f
        const val HEADING_SP = 17f
        const val BODY_SP = 15f
    }
}
