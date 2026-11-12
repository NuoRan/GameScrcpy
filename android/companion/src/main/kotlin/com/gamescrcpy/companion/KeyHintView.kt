package com.gamescrcpy.companion

import android.content.Context
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.util.TypedValue
import android.view.View
import com.gamescrcpy.protocol.Enums.KeyHintShape
import kotlin.math.max

class KeyHintView(context: Context) : View(context) {
    private val metrics = resources.displayMetrics
    private var items: List<HintItem> = emptyList()
    private var geometry: OverlayGeometry? = null
    private val rect = RectF()
    private val path = Path()
    private val font = Paint.FontMetrics()
    private val fill = paint(Paint.Style.FILL, KeyHintLook.FILL)
    private val echoFar = paint(Paint.Style.FILL, KeyHintLook.ECHO_FAR)
    private val outline = paint(Paint.Style.STROKE, KeyHintLook.STROKE).apply { strokeWidth = dp(KeyHintLook.STROKE_DP) }
    private val ringFill = paint(Paint.Style.FILL, KeyHintLook.RING_FILL)
    private val ringOutline = paint(Paint.Style.STROKE, KeyHintLook.RING).apply {
        strokeWidth = dp(KeyHintLook.RING_STROKE_DP)
    }
    private val halo = paint(Paint.Style.STROKE, KeyHintLook.TEXT).apply {
        strokeWidth = dp(KeyHintLook.RING_STROKE_DP)
        strokeCap = Paint.Cap.ROUND
    }
    private val dashed = paint(Paint.Style.STROKE, KeyHintLook.TEXT).apply {
        strokeWidth = dp(KeyHintLook.STROKE_DP)
        pathEffect = DashPathEffect(floatArrayOf(dp(KeyHintLook.RING_STROKE_DP + 1f), dp(KeyHintLook.RING_STROKE_DP)), 0f)
    }
    private val tick = paint(Paint.Style.FILL, KeyHintLook.TEXT)
    private val label = paint(Paint.Style.FILL, KeyHintLook.TEXT).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.DEFAULT, KeyHintLook.TEXT_WEIGHT, false)
        textSize = sp(KeyHintLook.TEXT_SP)
    }
    private val smallLabel = Paint(label).apply { textSize = sp(KeyHintLook.SMALL_TEXT_SP) }

    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        alpha = 0f
    }

    fun setItems(next: List<HintItem>) {
        items = next
        invalidate()
    }

    fun setGeometry(next: OverlayGeometry?) {
        geometry = next
        invalidate()
    }

    fun setState(state: HintState) {
        animate().cancel()
        val target = KeyHints.alphaOf(state)
        if (Look.reducedMotion()) {
            alpha = target
            return
        }
        animate().alpha(target).setDuration(Look.SLOW).setInterpolator(Look.standard).start()
    }

    override fun onDraw(canvas: Canvas) {
        val placed = geometry ?: return
        for (item in items) {
            val x = CursorMapper.place(item.x, placed.displayWidth, placed.left, placed.width)
            val y = CursorMapper.place(item.y, placed.displayHeight, placed.top, placed.height)
            when (item.shape) {
                KeyHintShape.RING -> drawRing(
                    canvas,
                    x,
                    y,
                    KeyHints.extent(item.width, placed.displayWidth) / 2,
                    KeyHints.extent(item.height, placed.displayHeight) / 2,
                )
                KeyHintShape.KEY -> drawKey(canvas, x, y, item.label)
                else -> drawRound(canvas, x, y, item)
            }
        }
    }

    private fun drawRing(canvas: Canvas, x: Float, y: Float, rx: Float, ry: Float) {
        if (rx <= 0f || ry <= 0f) {
            return
        }
        rect.set(x - rx, y - ry, x + rx, y + ry)
        canvas.drawOval(rect, ringFill)
        canvas.drawOval(rect, ringOutline)
    }

    private fun drawKey(canvas: Canvas, x: Float, y: Float, text: String) {
        val height = dp(KeyHintLook.KEY_DP)
        val width = max(height, smallLabel.measureText(text) + 2 * dp(KeyHintLook.KEY_PADDING_DP))
        val radius = dp(KeyHintLook.KEY_RADIUS_DP)
        rect.set(x - width / 2, y - height / 2, x + width / 2, y + height / 2)
        canvas.drawRoundRect(rect, radius, radius, fill)
        canvas.drawRoundRect(rect, radius, radius, outline)
        drawText(canvas, text, x, y, smallLabel)
    }

    private fun drawRound(canvas: Canvas, x: Float, y: Float, item: HintItem) {
        val height = dp(KeyHintLook.ROUND_DP)
        val textWidth = label.measureText(item.label)
        val freeLook = item.shape == KeyHintShape.FREE_LOOK
        val width = if (freeLook) {
            max(2 * height, textWidth + 2 * dp(KeyHintLook.KEY_PADDING_DP + KeyHintLook.PADDING_DP))
        } else {
            max(height, textWidth + 2 * dp(KeyHintLook.PADDING_DP))
        }
        val cap = height / 2
        val left = x - width / 2
        val top = y - cap
        if (item.shape == KeyHintShape.TURBO) {
            val shift = dp(KeyHintLook.ECHO_DP)
            pill(left + 2 * shift, top - 2 * shift, width, height)
            canvas.drawRoundRect(rect, cap, cap, echoFar)
            canvas.drawRoundRect(rect, cap, cap, outline)
            pill(left + shift, top - shift, width, height)
            canvas.drawRoundRect(rect, cap, cap, fill)
            canvas.drawRoundRect(rect, cap, cap, outline)
        }
        when (item.shape) {
            KeyHintShape.FREE_LOOK -> {
                path.reset()
                path.moveTo(left, y)
                path.cubicTo(left + width * 0.22f, top - height * 0.08f, left + width * 0.78f, top - height * 0.08f, left + width, y)
                path.cubicTo(left + width * 0.78f, top + height * 1.08f, left + width * 0.22f, top + height * 1.08f, left, y)
                path.close()
                canvas.drawPath(path, fill)
                canvas.drawPath(path, outline)
            }
            KeyHintShape.UNBOUND -> {
                pill(left, top, width, height)
                canvas.drawRoundRect(rect, cap, cap, dashed)
            }
            else -> {
                pill(left, top, width, height)
                canvas.drawRoundRect(rect, cap, cap, fill)
                canvas.drawRoundRect(rect, cap, cap, outline)
            }
        }
        val reach = dp(KeyHintLook.HALO_DP)
        when (item.shape) {
            KeyHintShape.HOLD -> {
                pill(left - reach, top - reach, width + 2 * reach, height + 2 * reach)
                canvas.drawRoundRect(rect, cap + reach, cap + reach, halo)
            }
            KeyHintShape.LONG_PRESS -> {
                pill(left - reach, top - reach, width + 2 * reach, height + 2 * reach)
                canvas.drawRoundRect(rect, cap + reach, cap + reach, outline)
                if (width <= height) {
                    canvas.drawArc(rect, LONG_PRESS_START, LONG_PRESS_SWEEP, false, halo)
                }
            }
            KeyHintShape.AIM -> {
                pill(left - reach, top - reach, width + 2 * reach, height + 2 * reach)
                canvas.drawRoundRect(rect, cap + reach, cap + reach, dashed)
            }
            KeyHintShape.CAMERA -> drawTicks(canvas, x, y, width, height)
            KeyHintShape.SWIPE -> drawSwipe(canvas, x + width / 2, y)
            else -> Unit
        }
        drawText(canvas, item.label, x, y, label)
    }

    private fun drawTicks(canvas: Canvas, x: Float, y: Float, width: Float, height: Float) {
        val thin = dp(KeyHintLook.RING_STROKE_DP)
        val long = dp(KeyHintLook.TICK_DP)
        val gap = dp(KeyHintLook.RING_STROKE_DP)
        canvas.drawRect(x - thin / 2, y - height / 2 - gap - long, x + thin / 2, y - height / 2 - gap, tick)
        canvas.drawRect(x - thin / 2, y + height / 2 + gap, x + thin / 2, y + height / 2 + gap + long, tick)
        canvas.drawRect(x - width / 2 - gap - long, y - thin / 2, x - width / 2 - gap, y + thin / 2, tick)
        canvas.drawRect(x + width / 2 + gap, y - thin / 2, x + width / 2 + gap + long, y + thin / 2, tick)
    }

    private fun drawSwipe(canvas: Canvas, start: Float, y: Float) {
        val end = start + dp(KeyHintLook.HALO_DP) + 3 * dp(KeyHintLook.TICK_DP)
        canvas.drawLine(start, y, end, y, dashed)
        val head = dp(KeyHintLook.TICK_DP) * 0.8f
        canvas.drawLine(end - head, y - head, end, y, halo)
        canvas.drawLine(end - head, y + head, end, y, halo)
    }

    private fun drawText(canvas: Canvas, text: String, x: Float, y: Float, paint: Paint) {
        if (text.isEmpty()) {
            return
        }
        paint.getFontMetrics(font)
        canvas.drawText(text, x, y - (font.ascent + font.descent) / 2, paint)
    }

    private fun pill(left: Float, top: Float, width: Float, height: Float) {
        rect.set(left, top, left + width, top + height)
    }

    private fun paint(style: Paint.Style, color: Int): Paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        this.style = style
        this.color = color
    }

    private fun dp(value: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, metrics)

    private fun sp(value: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, value, metrics)

    private companion object {
        const val LONG_PRESS_START = 225f
        const val LONG_PRESS_SWEEP = 270f
    }
}
