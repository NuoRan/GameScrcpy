package com.gamescrcpy.companion

import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

data class CursorSample(val x: Int, val y: Int, val visible: Boolean, val pressed: Boolean) {
    companion object {
        val HIDDEN = CursorSample(x = 0, y = 0, visible = false, pressed = false)
    }
}

fun interface CursorSink {
    fun update(sample: CursorSample)
}

class CursorCoalescer(private val requestFrame: () -> Unit) {
    private val latest = AtomicReference<CursorSample?>(null)
    private val pending = AtomicBoolean(false)

    fun offer(sample: CursorSample) {
        latest.set(sample)
        if (pending.compareAndSet(false, true)) {
            requestFrame()
        }
    }

    fun take(): CursorSample? {
        pending.set(false)
        return latest.get()
    }
}

data class OverlayGeometry(
    val displayWidth: Int,
    val displayHeight: Int,
    val left: Int,
    val top: Int,
    val width: Int,
    val height: Int,
)

object CursorMapper {
    const val FULL_SCALE = 65535f

    fun map(value: Int, size: Int): Float {
        if (size <= 0) {
            return 0f
        }
        return (value.toFloat() * size / FULL_SCALE).coerceIn(0f, size - 1f)
    }

    fun place(value: Int, displaySize: Int, origin: Int, viewSize: Int): Float {
        if (viewSize <= 0) {
            return 0f
        }
        return (map(value, displaySize) - origin).coerceIn(0f, viewSize - 1f)
    }

    fun x(sample: CursorSample, geometry: OverlayGeometry): Float =
        place(sample.x, geometry.displayWidth, geometry.left, geometry.width)

    fun y(sample: CursorSample, geometry: OverlayGeometry): Float =
        place(sample.y, geometry.displayHeight, geometry.top, geometry.height)
}

data class OverlayInfo(val granted: Boolean, val width: Int, val height: Int, val rotation: Int) {
    companion object {
        val NONE = OverlayInfo(granted = false, width = 0, height = 0, rotation = 0)
    }
}
