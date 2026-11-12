package com.gamescrcpy.companion

import android.content.Context
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.os.Build
import android.view.Display
import android.view.Gravity
import android.view.View
import android.view.WindowManager

class TopHint(context: Context, private val onTap: () -> Unit) {
    private val windowContext = windowContextOf(context)
    private val windowManager = windowContext.getSystemService(WindowManager::class.java)
    private var shown: View? = null

    fun show() {
        if (shown != null) {
            return
        }
        val ui = Ui(windowContext, Look.palette(windowContext, null))
        val pill = ui.text(Look.LABEL, ui.palette.textPrimary, R.string.ime_typing_top).apply {
            background = ui.pressable(ui.rounded(ui.palette.bgElevated, PILL_RADIUS, ui.palette.strokeStrong), PILL_RADIUS)
            gravity = Gravity.CENTER_VERTICAL
            minHeight = ui.px(Look.CONTROL)
            setPaddingRelative(ui.px(Look.SPACE_MD), ui.px(Look.SPACE_XS), ui.px(Look.SPACE_LG), ui.px(Look.SPACE_XS))
            ui.lead(this, R.drawable.ic_keyboard, ui.palette.accent)
            isClickable = true
            isFocusable = false
            setOnClickListener { onTap() }
        }
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT,
        )
        params.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        params.y = ui.px(Look.SPACE_SM)
        params.title = TITLE
        try {
            windowManager.addView(pill, params)
        } catch (e: RuntimeException) {
            return
        }
        shown = pill
        if (!Look.reducedMotion()) {
            pill.alpha = 0f
            pill.translationY = -Look.dp(windowContext, Look.PAGE_SHIFT)
            pill.animate().alpha(1f).translationY(0f).setDuration(Look.BASE).setInterpolator(Look.emphasized).start()
        }
    }

    fun hide() {
        val view = shown ?: return
        shown = null
        view.animate().cancel()
        try {
            windowManager.removeViewImmediate(view)
        } catch (e: RuntimeException) {
            return
        }
    }

    private companion object {
        const val TITLE = "GameScrcpy typing hint"
        const val PILL_RADIUS = 999f

        fun windowContextOf(context: Context): Context {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
                return context
            }
            val display = context.getSystemService(DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY)
                ?: return context
            return context.createDisplayContext(display)
                .createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null)
        }
    }
}
