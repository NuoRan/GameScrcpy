package com.gamescrcpy.companion

import android.view.View
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import com.gamescrcpy.protocol.Enums.CompanionItem
import kotlin.math.roundToInt

class FeaturesPage(shell: Shell) : Page(shell, R.string.features_title, R.string.features_subtitle) {
    private val toggles = HashMap<Feature, ToggleView>()
    private val notes = HashMap<Feature, TextView>()
    private val opacityValue = ui.text(Look.LABEL, ui.palette.textSecondary)
    private var dragging = false
    private val opacity: SeekBar = ui.slider(
        SyncValues.MIN_OPACITY,
        SyncValues.MAX_OPACITY,
        shell.sync.values.hintOpacity,
        { value ->
            dragging = true
            showOpacity(value)
        },
        { value ->
            dragging = false
            val snapped = ((value / OPACITY_STEP.toFloat()).roundToInt() * OPACITY_STEP)
                .coerceIn(SyncValues.MIN_OPACITY, SyncValues.MAX_OPACITY)
            showOpacity(snapped)
            shell.sync.change(CompanionItem.HINT_OPACITY, snapped)
            refresh()
        },
    )

    init {
        for (feature in Feature.entries) {
            body.addView(card(feature), wide())
        }
        body.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, R.string.features_synced).apply {
            setPaddingRelative(ui.px(Look.SPACE_XXS), 0, ui.px(Look.SPACE_XXS), 0)
        })
    }

    private fun card(feature: Feature): View {
        val card = ui.card(gap = Look.SPACE_MD)
        val head = ui.row(Look.SPACE_MD)
        head.addView(ui.tile(feature.icon, feature.hue))
        head.addView(ui.text(Look.HEADING, ui.palette.textPrimary, feature.title), weighted())
        val toggle = ui.toggle(shell.sync.values.valueOf(feature.item) != 0, context.getString(feature.title)) { on ->
            shell.sync.change(feature.item, if (on) 1 else 0)
        }
        toggles[feature] = toggle
        head.addView(toggle)
        card.addView(head, wide())
        card.addView(ui.text(Look.BODY, ui.palette.textSecondary, feature.about))
        val note = ui.text(Look.CAPTION, ui.palette.warningText, R.string.home_not_sent)
        notes[feature] = note
        card.addView(note)
        when (feature) {
            Feature.CURSOR -> {
                card.addView(ui.divider())
                option(card, R.string.option_cursor_size, ui.segments(
                    listOf(R.string.option_small, R.string.option_medium, R.string.option_large),
                    shell.options.cursorSize.ordinal,
                ) { shell.options.chooseCursorSize(CursorSize.entries[it]) })
                option(card, R.string.option_cursor_style, ui.segments(
                    listOf(R.string.option_arrow, R.string.option_dot),
                    shell.options.cursorStyle.ordinal,
                ) { shell.options.chooseCursorStyle(CursorStyle.entries[it]) })
            }
            Feature.HINTS -> {
                card.addView(ui.divider())
                val line = ui.row(Look.SPACE_SM)
                line.addView(opacity, weighted())
                opacityValue.minWidth = ui.px(VALUE_WIDTH)
                opacityValue.textAlignment = View.TEXT_ALIGNMENT_VIEW_END
                line.addView(opacityValue)
                option(card, R.string.option_opacity, line)
                showOpacity(shell.sync.values.hintOpacity)
            }
            Feature.KEYBOARD -> {
                card.addView(ui.divider())
                option(card, R.string.option_hint_bar, ui.segments(
                    listOf(R.string.option_bottom, R.string.option_top),
                    shell.options.imeBar.ordinal,
                ) { shell.options.chooseImeBar(ImeBar.entries[it]) })
                card.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, R.string.option_hint_bar_note))
            }
            Feature.AUTO_CONNECT -> Unit
        }
        return card
    }

    private fun option(card: LinearLayout, label: Int, control: View) {
        val block = ui.column(Look.SPACE_SM)
        block.addView(ui.text(Look.LABEL, ui.palette.textPrimary, label))
        block.addView(control, wide())
        card.addView(block, wide())
    }

    private fun showOpacity(value: Int) {
        opacityValue.text = context.getString(R.string.option_percent, value)
    }

    override fun refresh() {
        val values = shell.sync.values
        val unsent = shell.sync.pendingItems
        val delivering = shell.sync.connected != null
        for ((feature, toggle) in toggles) {
            val on = values.valueOf(feature.item) != 0
            if (toggle.isChecked() != on) {
                toggle.set(on, animate = true)
            }
            notes[feature]?.visibility =
                if (feature.item in unsent && !delivering) View.VISIBLE else View.GONE
        }
        if (!dragging && opacity.progress != values.hintOpacity) {
            opacity.progress = values.hintOpacity
            showOpacity(values.hintOpacity)
        }
    }

    private companion object {
        const val OPACITY_STEP = 5
        const val VALUE_WIDTH = 48f
    }
}
