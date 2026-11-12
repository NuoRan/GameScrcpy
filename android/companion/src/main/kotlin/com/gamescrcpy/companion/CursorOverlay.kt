package com.gamescrcpy.companion

import android.content.Context
import android.graphics.PixelFormat
import android.graphics.Point
import android.hardware.display.DisplayManager
import android.hardware.input.InputManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Choreographer
import android.view.Display
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout

class CursorOverlay(context: Context, private val report: (OverlayInfo) -> Unit) : CursorSink {
    private val displays = context.getSystemService(DisplayManager::class.java)
    private val windowContext = windowContextOf(context, displays)
    private val windowManager = windowContext.getSystemService(WindowManager::class.java)
    private val choreographer = Choreographer.getInstance()
    private val coalescer = CursorCoalescer(::requestFrame)
    private val frame = Choreographer.FrameCallback { show(coalescer.take()) }
    private val root = FrameLayout(windowContext)
    private val hints = KeyHintView(windowContext)
    private val cursor = CursorView(windowContext)
    private var geometry: OverlayGeometry? = null
    private var attached = false
    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) = Unit

        override fun onDisplayRemoved(displayId: Int) = Unit

        override fun onDisplayChanged(displayId: Int) {
            if (displayId == Display.DEFAULT_DISPLAY) {
                measure()
            }
        }
    }

    init {
        root.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        root.addView(
            hints,
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT),
        )
        root.addView(
            cursor,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP or Gravity.START,
            ),
        )
        root.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> measure() }
    }

    fun attach(): Boolean {
        if (attached) {
            return true
        }
        try {
            windowManager.addView(root, layoutParams())
        } catch (e: RuntimeException) {
            return false
        }
        attached = true
        displays.registerDisplayListener(displayListener, Handler(Looper.getMainLooper()))
        return true
    }

    fun detach() {
        if (!attached) {
            return
        }
        attached = false
        geometry = null
        hints.setGeometry(null)
        displays.unregisterDisplayListener(displayListener)
        choreographer.removeFrameCallback(frame)
        windowManager.removeViewImmediate(root)
    }

    override fun update(sample: CursorSample) {
        coalescer.offer(sample)
    }

    fun showHints(items: List<HintItem>) {
        hints.setItems(items)
    }

    fun hintState(state: HintState) {
        hints.setState(state)
    }

    fun cursorLook(size: CursorSize, style: CursorStyle) {
        cursor.setLook(size, style)
    }

    private fun requestFrame() {
        choreographer.postFrameCallback(frame)
    }

    private fun show(sample: CursorSample?) {
        val placed = geometry
        if (!attached || placed == null || sample == null || !sample.visible) {
            cursor.visibility = View.INVISIBLE
            return
        }
        cursor.translationX = CursorMapper.x(sample, placed) - cursor.tip
        cursor.translationY = CursorMapper.y(sample, placed) - cursor.tip
        val scale = if (sample.pressed) PRESSED_SCALE else 1f
        cursor.scaleX = scale
        cursor.scaleY = scale
        cursor.visibility = View.VISIBLE
    }

    private fun measure() {
        if (!attached || root.width <= 0 || root.height <= 0) {
            return
        }
        val origin = IntArray(2)
        root.getLocationOnScreen(origin)
        val display = displaySize()
        val placed = OverlayGeometry(display.x, display.y, origin[0], origin[1], root.width, root.height)
        geometry = placed
        hints.setGeometry(placed)
        report(OverlayInfo(granted = true, width = root.width, height = root.height, rotation = rotationOf(displays)))
        requestFrame()
    }

    private fun displaySize(): Point {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val bounds = windowManager.maximumWindowMetrics.bounds
            return Point(bounds.width(), bounds.height())
        }
        return realSizeOf(displays.getDisplay(Display.DEFAULT_DISPLAY))
    }

    @Suppress("DEPRECATION")
    private fun realSizeOf(display: Display?): Point = Point().also { display?.getRealSize(it) }

    private fun layoutParams(): WindowManager.LayoutParams {
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT,
        )
        params.gravity = Gravity.TOP or Gravity.START
        params.title = TITLE
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            params.fitInsetsTypes = 0
        } else {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val limit = windowContext.getSystemService(InputManager::class.java).maximumObscuringOpacityForTouch
            params.alpha = (limit - OPACITY_MARGIN).coerceIn(0f, 1f)
        }
        return params
    }

    companion object {
        private const val TITLE = "GameScrcpy cursor"
        private const val PRESSED_SCALE = 0.85f
        private const val OPACITY_MARGIN = 0.01f

        fun rotationOf(displays: DisplayManager): Int = displays.getDisplay(Display.DEFAULT_DISPLAY)?.rotation ?: 0

        private fun windowContextOf(context: Context, displays: DisplayManager): Context {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
                return context
            }
            val display = displays.getDisplay(Display.DEFAULT_DISPLAY) ?: return context
            return context.createDisplayContext(display)
                .createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null)
        }
    }
}
