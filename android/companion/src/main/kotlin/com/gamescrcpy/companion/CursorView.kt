package com.gamescrcpy.companion

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import kotlin.math.ceil

class CursorView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) : View(context, attrs) {
    private val density = resources.displayMetrics.density
    private val line = OUTLINE * density
    var tip = line
        private set
    private var size = CursorSize.MEDIUM
    private var style = CursorStyle.ARROW
    private val shape = Path()
    private val bounds = RectF()
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        style = Paint.Style.FILL
    }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeWidth = line
    }

    init {
        build()
        visibility = INVISIBLE
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    fun setLook(size: CursorSize, style: CursorStyle) {
        if (size == this.size && style == this.style) {
            return
        }
        this.size = size
        this.style = style
        build()
        requestLayout()
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(ceil(bounds.right + line).toInt(), ceil(bounds.bottom + line).toInt())
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawPath(shape, outline)
        canvas.drawPath(shape, fill)
    }

    private fun build() {
        val unit = density * size.scale
        shape.reset()
        if (style == CursorStyle.DOT) {
            val radius = DOT_RADIUS * unit
            tip = line + radius
            shape.addCircle(tip, tip, radius, Path.Direction.CW)
        } else {
            tip = line
            shape.moveTo(0f, 0f)
            for (i in SHAPE.indices step 2) {
                shape.lineTo(SHAPE[i] * unit, SHAPE[i + 1] * unit)
            }
            shape.close()
            shape.offset(tip, tip)
        }
        shape.computeBounds(bounds, true)
        pivotX = tip
        pivotY = tip
    }

    private companion object {
        const val OUTLINE = 1.5f
        const val DOT_RADIUS = 6f
        val SHAPE = floatArrayOf(0f, 17f, 4.2f, 13.2f, 7f, 19.6f, 9.6f, 18.4f, 6.8f, 12.2f, 12f, 12f)
    }
}
