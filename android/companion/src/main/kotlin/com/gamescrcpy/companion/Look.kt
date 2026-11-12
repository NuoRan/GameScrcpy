package com.gamescrcpy.companion

import android.animation.ValueAnimator
import android.content.Context
import android.content.res.Configuration
import android.graphics.Typeface
import android.os.Build
import android.util.TypedValue
import android.view.animation.PathInterpolator
import android.widget.TextView
import kotlin.math.roundToInt

data class Palette(
    val dark: Boolean,
    val bgBase: Int,
    val bgRaised: Int,
    val bgOverlay: Int,
    val bgElevated: Int,
    val bgSunken: Int,
    val strokeSubtle: Int,
    val strokeDefault: Int,
    val strokeStrong: Int,
    val stateHover: Int,
    val statePress: Int,
    val highlight: Int,
    val knob: Int,
    val textPrimary: Int,
    val textSecondary: Int,
    val textTertiary: Int,
    val textDisabled: Int,
    val onAccent: Int,
    val accent: Int,
    val accentText: Int,
    val success: Int,
    val warning: Int,
    val danger: Int,
    val info: Int,
    val successText: Int,
    val warningText: Int,
    val dangerText: Int,
    val infoText: Int,
) {
    val accentSubtle: Int
        get() = Look.alpha(accent, ACCENT_SUBTLE)

    val accentLine: Int
        get() = Look.alpha(accent, ACCENT_LINE)

    fun withAccent(rgb: Int): Palette {
        val opaque = Look.OPAQUE or (rgb and Look.RGB)
        return copy(accent = opaque, accentText = if (dark) opaque else Look.accentTextOnLight(opaque))
    }

    fun ink(tone: Tone): Int = when (tone) {
        Tone.LIVE -> accentText
        Tone.OK -> successText
        Tone.WARN -> warningText
        Tone.BAD -> dangerText
        Tone.IDLE -> textSecondary
    }

    fun fill(tone: Tone): Int = when (tone) {
        Tone.LIVE -> accentSubtle
        Tone.OK -> Look.alpha(success, STATE_SUBTLE)
        Tone.WARN -> Look.alpha(warning, WARNING_SUBTLE)
        Tone.BAD -> Look.alpha(danger, STATE_SUBTLE)
        Tone.IDLE -> statePress
    }

    fun tileFill(hue: Int): Int = Look.alpha(hue, if (dark) TILE_DARK else TILE_LIGHT)

    fun tileInk(hue: Int): Int = if (dark) Look.OPAQUE or (hue and Look.RGB) else Look.accentTextOnLight(hue)

    companion object {
        private const val ACCENT_SUBTLE = 0.14f
        private const val ACCENT_LINE = 0.45f
        private const val STATE_SUBTLE = 0.12f
        private const val WARNING_SUBTLE = 0.13f
        private const val TILE_DARK = 0.16f
        private const val TILE_LIGHT = 0.12f

        fun night(): Palette = Palette(
            dark = true,
            bgBase = 0xFF0B0C0F.toInt(),
            bgRaised = 0xFF111317.toInt(),
            bgOverlay = 0xFF171A1F.toInt(),
            bgElevated = 0xFF1D2127.toInt(),
            bgSunken = 0xFF08090B.toInt(),
            strokeSubtle = 0x0FFFFFFF,
            strokeDefault = 0x1AFFFFFF,
            strokeStrong = 0x29FFFFFF,
            stateHover = 0x0AFFFFFF,
            statePress = 0x14FFFFFF,
            highlight = 0x0BFFFFFF,
            knob = 0xFFEDEFF3.toInt(),
            textPrimary = 0xFFEDEFF3.toInt(),
            textSecondary = 0xFFA3A9B6.toInt(),
            textTertiary = 0xFF7F8693.toInt(),
            textDisabled = 0xFF4A505A.toInt(),
            onAccent = 0xFF0B0C0F.toInt(),
            accent = Look.EMBER,
            accentText = Look.EMBER,
            success = Look.SUCCESS,
            warning = Look.WARNING,
            danger = Look.DANGER,
            info = Look.INFO,
            successText = Look.SUCCESS,
            warningText = Look.WARNING,
            dangerText = Look.DANGER,
            infoText = Look.INFO,
        )

        fun day(): Palette = Palette(
            dark = false,
            bgBase = 0xFFE8EBEF.toInt(),
            bgRaised = 0xFFF4F5F8.toInt(),
            bgOverlay = 0xFFFFFFFF.toInt(),
            bgElevated = 0xFFFFFFFF.toInt(),
            bgSunken = 0xFFE7EAEF.toInt(),
            strokeSubtle = 0x17000000,
            strokeDefault = 0x1F000000,
            strokeStrong = 0x29000000,
            stateHover = 0x09000000,
            statePress = 0x12000000,
            highlight = 0xE6FFFFFF.toInt(),
            knob = 0xFFFFFFFF.toInt(),
            textPrimary = 0xFF111318.toInt(),
            textSecondary = 0xFF474E5C.toInt(),
            textTertiary = 0xFF5E6572.toInt(),
            textDisabled = 0xFFB4BAC4.toInt(),
            onAccent = 0xFF0B0C0F.toInt(),
            accent = Look.EMBER,
            accentText = 0xFFC2461C.toInt(),
            success = Look.SUCCESS,
            warning = Look.WARNING,
            danger = Look.DANGER,
            info = Look.INFO,
            successText = 0xFF12805C.toInt(),
            warningText = 0xFFA15C00.toInt(),
            dangerText = 0xFFC8323A.toInt(),
            infoText = 0xFF2F6BD9.toInt(),
        )
    }
}

enum class Tone { LIVE, OK, WARN, BAD, IDLE }

class TextStyle(val size: Float, val line: Float, val weight: Int)

object Look {
    const val OPAQUE = 0xFF000000.toInt()
    const val RGB = 0xFFFFFF
    const val EMBER = 0xFFFF6B3D.toInt()
    const val TEAL = 0xFF22D3C5.toInt()
    const val AZURE = 0xFF4C8DFF.toInt()
    const val LIME = 0xFFB8F34A.toInt()
    const val ROSE = 0xFFFF4D8D.toInt()
    const val AMBER = 0xFFFFB020.toInt()
    const val SUCCESS = 0xFF3DDC97.toInt()
    const val WARNING = 0xFFFFB020.toInt()
    const val DANGER = 0xFFFF5A5F.toInt()
    const val INFO = 0xFF4C8DFF.toInt()

    val DISPLAY = TextStyle(28f, 34f, 600)
    val SUBTITLE = TextStyle(16f, 22f, 600)
    val HEADING = TextStyle(15f, 20f, 600)
    val BODY = TextStyle(14f, 20f, 400)
    val LABEL = TextStyle(13f, 18f, 500)
    val CAPTION = TextStyle(12f, 16f, 400)
    val MICRO = TextStyle(11f, 14f, 600)

    const val SPACE_XXS = 2f
    const val SPACE_XS = 4f
    const val SPACE_SM = 8f
    const val SPACE_MD = 12f
    const val SPACE_LG = 16f
    const val SPACE_XL = 20f
    const val SPACE_XXL = 24f

    const val RADIUS_SM = 6f
    const val RADIUS_MD = 8f
    const val RADIUS_LG = 10f
    const val RADIUS_XL = 14f

    const val STROKE = 1f
    const val CONTROL = 40f
    const val CONTROL_LARGE = 48f
    const val SWITCH_WIDTH = 40f
    const val SWITCH_HEIGHT = 22f
    const val SWITCH_KNOB = 14f
    const val PILL = 20f
    const val PILL_DOT = 6f
    const val TILE = 28f
    const val TILE_LARGE = 40f
    const val ICON_SMALL = 16f
    const val ICON = 20f
    const val NAV_HEIGHT = 64f
    const val ROW_MIN = 56f
    const val SLIDER_TRACK = 4f
    const val SLIDER_KNOB = 16f
    const val STATUS_DOT = 10f

    const val FAST = 120L
    const val BASE = 200L
    const val SLOW = 400L
    const val PAGE_SHIFT = 8f

    val standard = PathInterpolator(0.2f, 0f, 0f, 1f)
    val emphasized = PathInterpolator(0.05f, 0.7f, 0.1f, 1f)
    val exit = PathInterpolator(0.3f, 0f, 1f, 1f)

    private val accentTexts = mapOf(
        EMBER to 0xFFC2461C.toInt(),
        TEAL to 0xFF0E7C74.toInt(),
        AZURE to 0xFF2F6BD9.toInt(),
        LIME to 0xFF4D7A0A.toInt(),
        ROSE to 0xFFC21F5B.toInt(),
        AMBER to 0xFFA15C00.toInt(),
    )
    private const val DARKEN = 0.62f

    fun dark(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return true
        }
        val night = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        return night != Configuration.UI_MODE_NIGHT_NO
    }

    fun palette(context: Context, accent: Int?): Palette {
        val base = if (dark(context)) Palette.night() else Palette.day()
        return if (accent == null) base else base.withAccent(accent)
    }

    fun reducedMotion(): Boolean = !ValueAnimator.areAnimatorsEnabled()

    fun alpha(color: Int, amount: Float): Int = ((amount * 255).roundToInt() shl 24) or (color and RGB)

    fun blend(from: Int, to: Int, amount: Float): Int {
        val t = amount.coerceIn(0f, 1f)
        fun channel(shift: Int): Int {
            val a = (from ushr shift) and 0xFF
            val b = (to ushr shift) and 0xFF
            return ((a + (b - a) * t).roundToInt() and 0xFF) shl shift
        }
        return channel(24) or channel(16) or channel(8) or channel(0)
    }

    fun accentTextOnLight(rgb: Int): Int {
        val opaque = OPAQUE or (rgb and RGB)
        return accentTexts[opaque] ?: blend(opaque, OPAQUE, 1f - DARKEN)
    }

    fun dp(context: Context, value: Float): Float =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, context.resources.displayMetrics)

    fun px(context: Context, value: Float): Int = dp(context, value).roundToInt()

    fun style(view: TextView, style: TextStyle) {
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, style.size)
        view.typeface = Typeface.create(Typeface.DEFAULT, style.weight, false)
        view.setLineHeight(
            TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, style.line, view.resources.displayMetrics)
                .roundToInt(),
        )
        view.includeFontPadding = false
    }
}
