package com.gamescrcpy.companion

import android.Manifest
import android.app.NotificationManager
import android.content.Context
import android.content.pm.PackageManager
import android.provider.Settings
import android.view.inputmethod.InputMethodManager

enum class ImeStatus { CHOSEN, ENABLED, OFF }

object PhonePermissions {
    fun overlay(context: Context): Boolean = Settings.canDrawOverlays(context)

    fun notifications(context: Context): Boolean =
        context.getSystemService(NotificationManager::class.java)?.areNotificationsEnabled() == true

    fun camera(context: Context): Boolean =
        context.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    fun capture(context: Context): Boolean = overlay(context) || notifications(context)

    fun ime(context: Context): ImeStatus {
        val chosen = Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        if (chosen?.startsWith("${context.packageName}/") == true) {
            return ImeStatus.CHOSEN
        }
        val methods = context.getSystemService(InputMethodManager::class.java) ?: return ImeStatus.OFF
        return if (methods.enabledInputMethodList.any { it.packageName == context.packageName }) {
            ImeStatus.ENABLED
        } else {
            ImeStatus.OFF
        }
    }

    fun missing(context: Context): List<Int> = buildList {
        if (!overlay(context)) {
            add(R.string.perm_overlay)
        }
        if (!notifications(context)) {
            add(R.string.perm_notifications)
        }
    }
}
