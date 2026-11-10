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
    val tip = OUTLINE * density
    private val arrow = Path()
    private val bounds = RectF()
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        style = Paint.Style.FILL
    }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeWidth = OUTLINE * density
    }

    init {
        val unit = density * SCALE
        arrow.moveTo(0f, 0f)
        for (i in SHAPE.indices step 2) {
            arrow.lineTo(SHAPE[i] * unit, SHAPE[i + 1] * unit)
        }
        arrow.close()
        arrow.offset(tip, tip)
        arrow.computeBounds(bounds, true)
        pivotX = tip
        pivotY = tip
        visibility = INVISIBLE
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(ceil(bounds.right + tip).toInt(), ceil(bounds.bottom + tip).toInt())
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawPath(arrow, outline)
        canvas.drawPath(arrow, fill)
    }

    private companion object {
        const val SCALE = 1.25f
        const val OUTLINE = 1.5f
        val SHAPE = floatArrayOf(0f, 17f, 4.2f, 13.2f, 7f, 19.6f, 9.6f, 18.4f, 6.8f, 12.2f, 12f, 12f)
    }
}
