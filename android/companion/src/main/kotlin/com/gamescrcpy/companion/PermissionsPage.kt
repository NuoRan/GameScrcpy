package com.gamescrcpy.companion

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.TextView

class PermissionsPage(shell: Shell) : Page(shell, R.string.permissions_title, R.string.permissions_subtitle) {
    private enum class Kind(val title: Int, val about: Int, val icon: Int) {
        OVERLAY(R.string.perm_overlay, R.string.perm_overlay_about, R.drawable.ic_layers),
        NOTIFICATIONS(R.string.perm_notifications, R.string.perm_notifications_about, R.drawable.ic_bell),
        CAMERA(R.string.perm_camera, R.string.perm_camera_about, R.drawable.ic_camera),
        INPUT_METHOD(R.string.perm_input_method, R.string.perm_input_method_about, R.drawable.ic_keyboard),
        CAPTURE(R.string.perm_capture, R.string.perm_capture_about, R.drawable.ic_capture),
    }

    private val pills = HashMap<Kind, TextView>()

    init {
        for (kind in Kind.entries) {
            body.addView(card(kind), wide())
        }
    }

    private fun card(kind: Kind): View {
        val card = ui.card(gap = Look.SPACE_SM)
        val head = ui.row(Look.SPACE_MD)
        head.addView(ui.tile(kind.icon, null))
        head.addView(ui.text(Look.HEADING, ui.palette.textPrimary, kind.title), weighted())
        val pill = ui.pill(Tone.IDLE)
        pills[kind] = pill
        head.addView(pill)
        card.addView(head, wide())
        card.addView(ui.text(Look.BODY, ui.palette.textSecondary, kind.about))
        val actions = ui.row(Look.SPACE_SM).apply { gravity = Gravity.END or Gravity.CENTER_VERTICAL }
        if (kind == Kind.INPUT_METHOD) {
            actions.addView(ui.button(ButtonKind.GHOST, R.string.ime_choose).apply {
                setOnClickListener { context.getSystemService(InputMethodManager::class.java)?.showInputMethodPicker() }
            })
        }
        actions.addView(ui.button(ButtonKind.SECONDARY, R.string.perm_open).apply {
            setOnClickListener { open(kind) }
        })
        card.addView(actions, wide())
        return card
    }

    private fun open(kind: Kind) {
        val activity = shell.activity
        val app = Uri.fromParts("package", activity.packageName, null)
        when (kind) {
            Kind.OVERLAY, Kind.CAPTURE -> launch(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, app))
            Kind.NOTIFICATIONS -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                    !PhonePermissions.notifications(activity) &&
                    shell.ask(Manifest.permission.POST_NOTIFICATIONS)
                ) {
                    return
                }
                launch(Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                    .putExtra(Settings.EXTRA_APP_PACKAGE, activity.packageName))
            }
            Kind.CAMERA -> {
                if (!PhonePermissions.camera(activity) && shell.ask(Manifest.permission.CAMERA)) {
                    return
                }
                launch(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, app))
            }
            Kind.INPUT_METHOD -> launch(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS))
        }
    }

    private fun launch(intent: Intent) {
        try {
            shell.activity.startActivity(intent)
        } catch (e: ActivityNotFoundException) {
            shell.activity.startActivity(Intent(Settings.ACTION_SETTINGS))
        }
    }

    override fun refresh() {
        for ((kind, pill) in pills) {
            val (tone, text) = when (kind) {
                Kind.OVERLAY -> granted(PhonePermissions.overlay(context))
                Kind.NOTIFICATIONS -> granted(PhonePermissions.notifications(context))
                Kind.CAMERA -> if (PhonePermissions.camera(context)) {
                    Tone.OK to R.string.perm_allowed
                } else {
                    Tone.IDLE to R.string.perm_when_scanning
                }
                Kind.INPUT_METHOD -> when (PhonePermissions.ime(context)) {
                    ImeStatus.CHOSEN -> Tone.OK to R.string.perm_ime_chosen
                    ImeStatus.ENABLED -> Tone.WARN to R.string.perm_ime_enabled
                    ImeStatus.OFF -> Tone.IDLE to R.string.perm_ime_off
                }
                Kind.CAPTURE -> if (PhonePermissions.capture(context)) {
                    Tone.OK to R.string.perm_capture_ready
                } else {
                    Tone.WARN to R.string.perm_capture_blocked
                }
            }
            ui.tint(pill, tone)
            pill.setText(text)
        }
    }

    private fun granted(on: Boolean): Pair<Tone, Int> =
        if (on) Tone.OK to R.string.perm_allowed else Tone.WARN to R.string.perm_missing
}
