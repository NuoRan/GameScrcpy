package com.gamescrcpy.companion

import android.animation.ValueAnimator
import android.app.Dialog
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.drawable.ClipDrawable
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.graphics.drawable.RippleDrawable
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Checkable
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView

enum class ButtonKind { PRIMARY, SECONDARY, GHOST, DANGER }

class ToggleView(context: Context) : View(context), Checkable {
    var palette: Palette = Palette.night()
        set(value) {
            field = value
            invalidate()
        }

    var onToggle: ((Boolean) -> Unit)? = null

    private var on = false
    private var position = 0f
    private var animator: ValueAnimator? = null
    private val trackWidth = Look.dp(context, Look.SWITCH_WIDTH)
    private val trackHeight = Look.dp(context, Look.SWITCH_HEIGHT)
    private val knobSize = Look.dp(context, Look.SWITCH_KNOB)
    private val stroke = Look.dp(context, Look.STROKE)
    private val touchWidth = Look.px(context, Look.ROW_MIN)
    private val touchHeight = Look.px(context, Look.CONTROL_LARGE)
    private val rect = RectF()
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val border = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val knob = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    init {
        isClickable = true
        isFocusable = true
        setOnClickListener {
            toggle()
            onToggle?.invoke(on)
        }
    }

    override fun isChecked(): Boolean = on

    override fun setChecked(checked: Boolean) {
        set(checked, animate = false)
    }

    override fun toggle() {
        set(!on, animate = true)
    }

    fun set(checked: Boolean, animate: Boolean) {
        if (checked == on && animator == null) {
            return
        }
        on = checked
        animator?.cancel()
        animator = null
        val target = if (checked) 1f else 0f
        if (!animate || Look.reducedMotion() || !isAttachedToWindow) {
            position = target
            invalidate()
        } else {
            animator = ValueAnimator.ofFloat(position, target).apply {
                duration = Look.BASE
                interpolator = Look.emphasized
                addUpdateListener {
                    position = it.animatedValue as Float
                    invalidate()
                }
                start()
            }
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(resolveSize(touchWidth, widthMeasureSpec), resolveSize(touchHeight, heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        val left = (width - trackWidth) / 2
        val top = (height - trackHeight) / 2
        val radius = trackHeight / 2
        rect.set(left, top, left + trackWidth, top + trackHeight)
        fill.color = Look.blend(palette.bgSunken, palette.accent, position)
        canvas.drawRoundRect(rect, radius, radius, fill)
        if (position < 1f) {
            border.strokeWidth = stroke
            border.color = Look.blend(palette.strokeStrong, palette.strokeStrong and Look.RGB, position)
            rect.inset(stroke / 2, stroke / 2)
            canvas.drawRoundRect(rect, radius, radius, border)
        }
        val inset = (trackHeight - knobSize) / 2
        val travel = trackWidth - knobSize - 2 * inset
        knob.color = Look.blend(palette.textSecondary, palette.onAccent, position)
        canvas.drawCircle(left + inset + knobSize / 2 + position * travel, top + trackHeight / 2, knobSize / 2, knob)
    }

    override fun getAccessibilityClassName(): CharSequence = Switch::class.java.name

    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.isCheckable = true
        info.isChecked = on
    }

    override fun onInitializeAccessibilityEvent(event: AccessibilityEvent) {
        super.onInitializeAccessibilityEvent(event)
        event.isChecked = on
    }
}

class Ui(val context: Context, val palette: Palette) {
    fun px(value: Float): Int = Look.px(context, value)

    fun text(style: TextStyle, color: Int, resource: Int = 0): TextView = TextView(context).apply {
        Look.style(this, style)
        setTextColor(color)
        if (resource != 0) {
            setText(resource)
        }
    }

    fun column(gap: Float = 0f): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        if (gap > 0f) {
            showDividers = LinearLayout.SHOW_DIVIDER_MIDDLE
            dividerDrawable = spacer(gap)
        }
    }

    fun row(gap: Float = 0f): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        if (gap > 0f) {
            showDividers = LinearLayout.SHOW_DIVIDER_MIDDLE
            dividerDrawable = spacer(gap)
        }
    }

    fun spacer(size: Float): Drawable = GradientDrawable().apply {
        setColor(Color.TRANSPARENT)
        setSize(px(size), px(size))
    }

    fun rounded(color: Int, radius: Float, strokeColor: Int = Color.TRANSPARENT): GradientDrawable =
        GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = Look.dp(context, radius)
            setColor(color)
            if (Color.alpha(strokeColor) != 0) {
                setStroke(px(Look.STROKE), strokeColor)
            }
        }

    fun pressable(content: Drawable, radius: Float): Drawable =
        RippleDrawable(ColorStateList.valueOf(palette.statePress), content, rounded(Color.WHITE, radius))

    fun cardBackground(active: Boolean = false): Drawable {
        val face = rounded(palette.bgOverlay, Look.RADIUS_XL, if (active) palette.accentLine else palette.strokeSubtle)
        val shine = GradientDrawable().apply { setColor(palette.highlight) }
        return LayerDrawable(arrayOf<Drawable>(face, shine)).apply {
            setLayerGravity(1, Gravity.TOP or Gravity.FILL_HORIZONTAL)
            setLayerHeight(1, px(Look.STROKE))
            setLayerInsetRelative(1, px(Look.RADIUS_XL), px(Look.STROKE), px(Look.RADIUS_XL), 0)
        }
    }

    fun card(active: Boolean = false, gap: Float = Look.SPACE_MD): LinearLayout = column(gap).apply {
        background = cardBackground(active)
        setPaddingRelative(px(Look.SPACE_LG + Look.SPACE_XXS), px(Look.SPACE_LG), px(Look.SPACE_LG + Look.SPACE_XXS),
            px(Look.SPACE_LG))
    }

    fun pill(tone: Tone, resource: Int = 0): TextView = text(Look.MICRO, palette.ink(tone), resource).apply {
        tint(this, tone)
        gravity = Gravity.CENTER_VERTICAL
        minHeight = px(Look.PILL)
        setPaddingRelative(px(Look.SPACE_SM), 0, px(Look.SPACE_SM), 0)
        compoundDrawablePadding = px(Look.SPACE_XS + Look.SPACE_XXS)
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
    }

    fun tint(pill: TextView, tone: Tone) {
        pill.setTextColor(palette.ink(tone))
        pill.background = rounded(palette.fill(tone), Look.PILL / 2)
        val dot = if (tone == Tone.IDLE) {
            null
        } else {
            GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(palette.ink(tone))
                setBounds(0, 0, px(Look.PILL_DOT), px(Look.PILL_DOT))
            }
        }
        pill.setCompoundDrawablesRelative(dot, null, null, null)
    }

    fun button(kind: ButtonKind, resource: Int, icon: Int = 0, large: Boolean = false): TextView =
        text(Look.LABEL, inkOf(kind), resource).apply {
            if (kind == ButtonKind.PRIMARY) {
                typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, PRIMARY_WEIGHT, false)
            }
            gravity = Gravity.CENTER
            minHeight = px(if (large) Look.CONTROL_LARGE else Look.CONTROL)
            minimumHeight = minHeight
            setPaddingRelative(px(Look.SPACE_LG), 0, px(Look.SPACE_LG), 0)
            background = pressable(faceOf(kind), Look.RADIUS_SM)
            isClickable = true
            isFocusable = true
            maxLines = 1
            ellipsize = TextUtils.TruncateAt.END
            if (icon != 0) {
                lead(this, icon, inkOf(kind))
            }
        }

    fun lead(view: TextView, icon: Int, color: Int) {
        view.setCompoundDrawablesRelativeWithIntrinsicBounds(icon, 0, 0, 0)
        val drawable = view.compoundDrawablesRelative[0]?.mutate() ?: return
        drawable.setTint(color)
        drawable.setBounds(0, 0, px(Look.ICON_SMALL), px(Look.ICON_SMALL))
        view.setCompoundDrawablesRelative(drawable, null, null, null)
        view.compoundDrawablePadding = px(Look.SPACE_SM)
    }

    fun tile(icon: Int, hue: Int?, large: Boolean = false): FrameLayout {
        val size = if (large) Look.TILE_LARGE else Look.TILE
        val fill = if (hue == null) palette.statePress else palette.tileFill(hue)
        val ink = if (hue == null) palette.textSecondary else palette.tileInk(hue)
        return FrameLayout(context).apply {
            background = rounded(fill, if (large) Look.RADIUS_LG else Look.RADIUS_MD)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            val image = ImageView(context).apply {
                setImageResource(icon)
                imageTintList = ColorStateList.valueOf(ink)
            }
            val iconSize = px(if (large) Look.ICON else Look.ICON_SMALL)
            addView(image, FrameLayout.LayoutParams(iconSize, iconSize, Gravity.CENTER))
            layoutParams = LinearLayout.LayoutParams(px(size), px(size))
        }
    }

    fun divider(): View = View(context).apply {
        setBackgroundColor(palette.strokeSubtle)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(Look.STROKE))
    }

    fun toggle(checked: Boolean, label: CharSequence, onToggle: (Boolean) -> Unit): ToggleView =
        ToggleView(context).apply {
            palette = this@Ui.palette
            setChecked(checked)
            contentDescription = label
            this.onToggle = onToggle
        }

    fun segments(labels: List<Int>, selected: Int, onPick: (Int) -> Unit): LinearLayout {
        val group = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            background = rounded(palette.bgSunken, Look.RADIUS_MD)
            val inner = px(Look.SPACE_XXS)
            setPadding(inner, inner, inner, inner)
        }
        val items = labels.mapIndexed { index, label ->
            text(Look.LABEL, palette.textSecondary, label).apply {
                gravity = Gravity.CENTER
                minHeight = px(Look.CONTROL - 2 * Look.SPACE_XXS)
                isClickable = true
                isFocusable = true
                maxLines = 1
                ellipsize = TextUtils.TruncateAt.END
                setPaddingRelative(px(Look.SPACE_SM), 0, px(Look.SPACE_SM), 0)
                group.addView(this, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
                tag = index
            }
        }
        fun mark(chosen: Int) {
            items.forEachIndexed { index, item ->
                val on = index == chosen
                item.isSelected = on
                item.setTextColor(if (on) palette.textPrimary else palette.textSecondary)
                item.background = if (on) {
                    rounded(palette.bgElevated, Look.RADIUS_SM, palette.strokeDefault)
                } else {
                    pressable(ColorDrawable(Color.TRANSPARENT), Look.RADIUS_SM)
                }
            }
        }
        mark(selected)
        items.forEachIndexed { index, item ->
            item.setOnClickListener {
                mark(index)
                onPick(index)
            }
        }
        return group
    }

    fun slider(minimum: Int, maximum: Int, value: Int, onMove: (Int) -> Unit, onSet: (Int) -> Unit): SeekBar =
        SeekBar(context).apply {
            min = minimum
            max = maximum
            progress = value
            splitTrack = false
            val track = rounded(palette.strokeDefault, Look.SLIDER_TRACK / 2)
            val done = ClipDrawable(rounded(palette.accent, Look.SLIDER_TRACK / 2), Gravity.START, ClipDrawable.HORIZONTAL)
            progressDrawable = LayerDrawable(arrayOf<Drawable>(track, done)).apply {
                setId(0, android.R.id.background)
                setId(1, android.R.id.progress)
                for (layer in 0..1) {
                    setLayerGravity(layer, Gravity.CENTER_VERTICAL or Gravity.FILL_HORIZONTAL)
                    setLayerHeight(layer, px(Look.SLIDER_TRACK))
                }
            }
            thumb = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(palette.knob)
                setStroke(px(Look.STROKE), palette.strokeStrong)
                setSize(px(Look.SLIDER_KNOB), px(Look.SLIDER_KNOB))
            }
            minimumHeight = px(Look.CONTROL_LARGE)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                private var touching = false

                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (fromUser) {
                        onMove(progress)
                        if (!touching) {
                            onSet(progress)
                        }
                    }
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) {
                    touching = true
                }

                override fun onStopTrackingTouch(seekBar: SeekBar) {
                    touching = false
                    onSet(seekBar.progress)
                }
            })
        }

    fun confirm(title: CharSequence, message: Int, action: Int, onConfirm: () -> Unit) {
        val dialog = Dialog(context)
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
        val body = column(Look.SPACE_MD).apply {
            background = rounded(palette.bgElevated, Look.RADIUS_XL, palette.strokeStrong)
            val padding = px(Look.SPACE_XXL)
            setPadding(padding, padding, padding, px(Look.SPACE_LG))
            addView(text(Look.SUBTITLE, palette.textPrimary).apply { setText(title) })
            addView(text(Look.BODY, palette.textSecondary, message))
            val actions = row(Look.SPACE_SM).apply { gravity = Gravity.END or Gravity.CENTER_VERTICAL }
            actions.addView(button(ButtonKind.GHOST, R.string.action_cancel).apply {
                setOnClickListener { dialog.dismiss() }
            })
            actions.addView(button(ButtonKind.DANGER, action).apply {
                setOnClickListener {
                    dialog.dismiss()
                    onConfirm()
                }
            })
            addView(actions, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        dialog.setContentView(body)
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        dialog.show()
    }

    private fun faceOf(kind: ButtonKind): Drawable = when (kind) {
        ButtonKind.PRIMARY -> rounded(palette.accent, Look.RADIUS_SM)
        ButtonKind.SECONDARY -> rounded(palette.bgElevated, Look.RADIUS_SM, palette.strokeDefault)
        ButtonKind.GHOST -> ColorDrawable(Color.TRANSPARENT)
        ButtonKind.DANGER -> rounded(Look.alpha(palette.danger, DANGER_FILL), Look.RADIUS_SM,
            Look.alpha(palette.danger, DANGER_LINE))
    }

    private fun inkOf(kind: ButtonKind): Int = when (kind) {
        ButtonKind.PRIMARY -> palette.onAccent
        ButtonKind.SECONDARY -> palette.textPrimary
        ButtonKind.GHOST -> palette.textSecondary
        ButtonKind.DANGER -> palette.dangerText
    }

    private companion object {
        const val PRIMARY_WEIGHT = 600
        const val DANGER_FILL = 0.12f
        const val DANGER_LINE = 0.28f
    }
}

class NavBar(private val ui: Ui, private val onPick: (Int) -> Unit) {
    class Entry(val icon: Int, val label: Int)

    val view: LinearLayout = ui.row().apply {
        val face = ColorDrawable(ui.palette.bgBase)
        val edge = ColorDrawable(ui.palette.strokeSubtle)
        background = LayerDrawable(arrayOf<Drawable>(face, edge)).apply {
            setLayerGravity(1, Gravity.TOP or Gravity.FILL_HORIZONTAL)
            setLayerHeight(1, ui.px(Look.STROKE))
        }
    }

    private val items = ArrayList<Triple<LinearLayout, ImageView, TextView>>()

    fun add(entry: Entry) {
        val index = items.size
        val icon = ImageView(ui.context).apply {
            setImageResource(entry.icon)
            val padding = ui.px(Look.SPACE_XS)
            setPadding(ui.px(Look.SPACE_LG), padding, ui.px(Look.SPACE_LG), padding)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        val label = ui.text(Look.MICRO, ui.palette.textSecondary, entry.label).apply {
            gravity = Gravity.CENTER
            maxLines = 1
            ellipsize = TextUtils.TruncateAt.END
        }
        val item = ui.column(Look.SPACE_XXS).apply {
            gravity = Gravity.CENTER
            isClickable = true
            isFocusable = true
            contentDescription = ui.context.getString(entry.label)
            minimumHeight = ui.px(Look.NAV_HEIGHT)
            background = ui.pressable(ColorDrawable(Color.TRANSPARENT), Look.RADIUS_LG)
            addView(icon, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ui.px(Look.ICON + 2 * Look.SPACE_XS)))
            addView(label, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT))
            setOnClickListener { onPick(index) }
        }
        view.addView(item, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        items += Triple(item, icon, label)
    }

    fun select(selected: Int) {
        items.forEachIndexed { index, (item, icon, label) ->
            val on = index == selected
            item.isSelected = on
            icon.imageTintList = ColorStateList.valueOf(if (on) ui.palette.accent else ui.palette.textTertiary)
            icon.background = if (on) ui.rounded(ui.palette.stateHover, Look.RADIUS_LG) else null
            label.setTextColor(if (on) ui.palette.textPrimary else ui.palette.textSecondary)
        }
    }
}
