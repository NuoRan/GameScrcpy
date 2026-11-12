package com.gamescrcpy.companion

import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Chronometer
import android.widget.LinearLayout
import android.widget.TextView

class HomePage(shell: Shell) : Page(shell, R.string.app_name, R.string.home_subtitle) {
    private class Quick(val feature: Feature, val toggle: ToggleView, val pending: TextView)

    private val dot = View(context)
    private val state = ui.pill(Tone.IDLE)
    private val name = ui.text(Look.SUBTITLE, ui.palette.textPrimary)
    private val link = ui.text(Look.CAPTION, ui.palette.textSecondary)
    private val clock = Chronometer(context)
    private val note = ui.text(Look.BODY, ui.palette.textSecondary)
    private val older = ui.text(Look.CAPTION, ui.palette.warningText, R.string.home_older_computer)
    private val scan = ui.button(ButtonKind.PRIMARY, R.string.lan_scan, R.drawable.ic_scan, large = true)
    private val again = ui.button(ButtonKind.SECONDARY, R.string.lan_connect)
    private val stop = ui.button(ButtonKind.GHOST, R.string.notification_stop)
    private val connection = ui.card()
    private val quick = ArrayList<Quick>()
    private val missingTitle = ui.text(Look.LABEL, ui.palette.warningText, R.string.home_permissions_title)
    private val missingText = ui.text(Look.CAPTION, ui.palette.textSecondary)
    private val missing = ui.row(Look.SPACE_MD)

    init {
        dot.background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(ui.palette.textTertiary)
        }
        val head = ui.row(Look.SPACE_SM)
        head.addView(dot, LinearLayout.LayoutParams(ui.px(Look.STATUS_DOT), ui.px(Look.STATUS_DOT)))
        head.addView(state)
        connection.addView(head)
        connection.addView(name)
        Look.style(clock, Look.CAPTION)
        clock.setTextColor(ui.palette.textSecondary)
        clock.format = context.getString(R.string.home_connected_for)
        val facts = ui.row(Look.SPACE_SM)
        facts.addView(link)
        facts.addView(clock)
        connection.addView(facts)
        connection.addView(note)
        connection.addView(older)
        connection.addView(scan, wide())
        connection.addView(again, wide())
        connection.addView(stop, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT).apply { gravity = Gravity.END })
        scan.setOnClickListener { shell.scan() }
        again.setOnClickListener { shell.connectNow() }
        stop.setOnClickListener { shell.stopCompanion() }
        body.addView(connection, wide())

        body.addView(ui.text(Look.MICRO, ui.palette.textTertiary, R.string.home_switches).apply {
            setPaddingRelative(ui.px(Look.SPACE_XXS), ui.px(Look.SPACE_SM), 0, 0)
        })
        Feature.entries.chunked(2).forEach { pair ->
            val line = ui.row(Look.SPACE_MD).apply { gravity = Gravity.FILL_VERTICAL }
            pair.forEach { feature -> line.addView(quickCard(feature), LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.MATCH_PARENT, 1f)) }
            body.addView(line, wide())
        }
        body.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, R.string.home_switches_synced).apply {
            setPaddingRelative(ui.px(Look.SPACE_XXS), 0, ui.px(Look.SPACE_XXS), 0)
        })

        val texts = ui.column(Look.SPACE_XXS)
        texts.addView(missingTitle)
        texts.addView(missingText)
        missing.addView(texts, weighted())
        val review = ui.button(ButtonKind.GHOST, R.string.home_permissions_review)
        review.setOnClickListener { shell.open(Page.PERMISSIONS) }
        missing.addView(review)
        val bar = ColorDrawable(ui.palette.warning)
        missing.background = LayerDrawable(arrayOf<Drawable>(ui.cardBackground(), bar)).apply {
            setLayerGravity(1, Gravity.START or Gravity.FILL_VERTICAL)
            setLayerWidth(1, ui.px(ACCENT_BAR))
            setLayerInsetRelative(1, 0, ui.px(Look.RADIUS_XL), 0, ui.px(Look.RADIUS_XL))
        }
        missing.setPaddingRelative(ui.px(Look.SPACE_LG), ui.px(Look.SPACE_MD), ui.px(Look.SPACE_SM), ui.px(Look.SPACE_MD))
        body.addView(missing, wide())
    }

    private fun quickCard(feature: Feature): View {
        val card = ui.card(gap = Look.SPACE_XS)
        card.setPaddingRelative(ui.px(Look.SPACE_MD), ui.px(Look.SPACE_SM), ui.px(Look.SPACE_XS), ui.px(Look.SPACE_MD))
        val label = context.getString(feature.title)
        val toggle = ui.toggle(shell.sync.values.valueOf(feature.item) != 0, label) { on ->
            shell.sync.change(feature.item, if (on) 1 else 0)
        }
        val top = ui.row()
        top.addView(ui.tile(feature.icon, feature.hue))
        top.addView(View(context), weighted())
        top.addView(toggle)
        card.addView(top, wide())
        card.addView(ui.text(Look.LABEL, ui.palette.textPrimary, feature.title))
        card.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, feature.short).apply {
            maxLines = 2
            ellipsize = TextUtils.TruncateAt.END
            setPaddingRelative(0, 0, ui.px(Look.SPACE_SM), 0)
        })
        val pending = ui.text(Look.CAPTION, ui.palette.warningText, R.string.home_not_sent)
        card.addView(pending)
        quick += Quick(feature, toggle, pending)
        return card
    }

    override fun refresh() {
        val sync = shell.sync
        val computer = sync.connected
        val connected = CompanionState.connected
        val lan = CompanionState.lan
        val searching = lan == LanState.SEARCHING || lan == LanState.PAIRING
        val waiting = !connected && (CompanionState.listening || searching)
        val tone = when {
            connected -> Tone.OK
            waiting -> Tone.WARN
            lan == LanState.FAILED -> Tone.BAD
            else -> Tone.IDLE
        }
        ui.tint(state, tone)
        state.setText(
            when {
                connected -> R.string.state_connected
                searching -> R.string.state_searching
                waiting -> R.string.state_waiting
                else -> R.string.state_not_connected
            },
        )
        (dot.background as? GradientDrawable)?.setColor(
            when (tone) {
                Tone.OK -> ui.palette.success
                Tone.WARN -> ui.palette.warning
                Tone.BAD -> ui.palette.danger
                else -> ui.palette.textTertiary
            },
        )
        connection.background = ui.cardBackground(active = connected)
        when {
            connected && computer != null -> name.text = Labels.name(context, computer)
            connected -> name.setText(R.string.home_computer_generic)
            searching -> name.setText(R.string.home_looking)
            else -> name.setText(R.string.state_not_connected)
        }
        link.setText(Labels.link(computer, CompanionState.lanConnected && !CompanionState.localConnected))
        link.visibility = if (connected) View.VISIBLE else View.GONE
        if (connected) {
            clock.base = CompanionState.connectedClock
            clock.visibility = View.VISIBLE
            clock.start()
        } else {
            clock.stop()
            clock.visibility = View.GONE
        }
        val explanation = when {
            connected -> 0
            searching -> R.string.lan_searching
            lan == LanState.FAILED -> R.string.lan_failed
            CompanionState.listening -> R.string.home_waiting
            else -> R.string.home_idle
        }
        note.visibility = if (explanation == 0) View.GONE else View.VISIBLE
        if (explanation != 0) {
            note.setText(explanation)
        }
        older.visibility = if (connected && computer == null) View.VISIBLE else View.GONE
        val paired = PairingStore(context).all().isNotEmpty()
        scan.visibility = if (connected) View.GONE else View.VISIBLE
        again.visibility = if (paired && !CompanionState.lanConnected && !searching) View.VISIBLE else View.GONE
        stop.visibility = if (connected || waiting) View.VISIBLE else View.GONE

        val values = sync.values
        val unsent = sync.pendingItems
        val delivering = computer != null
        for (entry in quick) {
            val on = values.valueOf(entry.feature.item) != 0
            if (entry.toggle.isChecked() != on) {
                entry.toggle.set(on, animate = true)
            }
            entry.pending.visibility = if (entry.feature.item in unsent && !delivering) View.VISIBLE else View.GONE
        }

        val lacking = PhonePermissions.missing(context)
        missing.visibility = if (lacking.isEmpty()) View.GONE else View.VISIBLE
        missingText.text = lacking.joinToString(context.getString(R.string.list_separator)) { context.getString(it) }
    }

    private companion object {
        const val ACCENT_BAR = 3f
    }
}
