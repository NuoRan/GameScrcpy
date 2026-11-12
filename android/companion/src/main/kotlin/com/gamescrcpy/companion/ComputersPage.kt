package com.gamescrcpy.companion

import android.text.format.DateUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout

class ComputersPage(shell: Shell) : Page(shell, R.string.computers_title, R.string.computers_subtitle) {
    private val again = ui.button(ButtonKind.SECONDARY, R.string.lan_connect)
    private val list = ui.column(Look.SPACE_MD)
    private val empty = ui.card(gap = Look.SPACE_SM)

    init {
        val actions = ui.row(Look.SPACE_SM)
        val scan = ui.button(ButtonKind.PRIMARY, R.string.lan_scan, R.drawable.ic_scan)
        scan.setOnClickListener { shell.scan() }
        again.setOnClickListener { shell.connectNow() }
        actions.addView(scan, weighted())
        actions.addView(again, weighted())
        body.addView(actions, wide())
        body.addView(list, wide())
        empty.gravity = Gravity.CENTER_HORIZONTAL
        empty.addView(ui.tile(R.drawable.ic_nav_computers, null, large = true))
        empty.addView(ui.text(Look.SUBTITLE, ui.palette.textPrimary, R.string.computers_empty_title).apply {
            gravity = Gravity.CENTER
        })
        empty.addView(ui.text(Look.BODY, ui.palette.textSecondary, R.string.computers_empty_text).apply {
            gravity = Gravity.CENTER
        })
        body.addView(empty, wide())
    }

    override fun refresh() {
        list.removeAllViews()
        val store = PairingStore(context)
        val pairings = store.all()
        val known = shell.sync.known
        val connected = shell.sync.connected
        for (computer in known) {
            val pairing = pairings.firstOrNull { it.pc.isNotEmpty() && it.pc == computer.pc }
            list.addView(entry(computer, pairing, computer.id == connected?.id), wide())
        }
        for (pairing in pairings) {
            if (known.none { it.pc == pairing.pc }) {
                list.addView(entry(null, pairing, false), wide())
            }
        }
        val any = list.childCount > 0
        list.visibility = if (any) View.VISIBLE else View.GONE
        empty.visibility = if (any) View.GONE else View.VISIBLE
        again.visibility = if (pairings.isNotEmpty() && !CompanionState.lanConnected) View.VISIBLE else View.GONE
    }

    private fun entry(computer: KnownComputer?, pairing: Pairing?, live: Boolean): View {
        val card = ui.card(active = live, gap = Look.SPACE_MD)
        val head = ui.row(Look.SPACE_MD)
        head.addView(ui.tile(R.drawable.ic_nav_computers, computer?.accent?.let { Look.OPAQUE or it }))
        val texts = ui.column(Look.SPACE_XXS)
        val title = if (computer != null) Labels.name(context, computer) else context.getString(R.string.computers_lan_only)
        texts.addView(ui.text(Look.HEADING, ui.palette.textPrimary).apply { text = title })
        val seen = when {
            live -> context.getString(R.string.computers_connected_now)
            computer != null -> context.getString(
                R.string.computers_last_seen,
                DateUtils.getRelativeTimeSpanString(computer.lastSeen, System.currentTimeMillis(),
                    DateUtils.MINUTE_IN_MILLIS),
            )
            pairing != null -> context.getString(R.string.computers_last_address, "${pairing.host}:${pairing.port}")
            else -> ""
        }
        texts.addView(ui.text(Look.CAPTION, ui.palette.textSecondary).apply { text = seen })
        if (computer != null) {
            texts.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, Labels.link(computer, false)))
        }
        head.addView(texts, weighted())
        val pill = when {
            live -> ui.pill(Tone.OK, R.string.state_connected)
            pairing != null -> ui.pill(Tone.IDLE, R.string.computers_paired)
            else -> null
        }
        pill?.let { head.addView(it) }
        card.addView(head, wide())
        val remove = ui.button(ButtonKind.GHOST, R.string.computers_remove)
        remove.setOnClickListener {
            ui.confirm(context.getString(R.string.computers_remove_title, title), R.string.computers_remove_text,
                R.string.computers_remove) {
                computer?.let { known -> shell.sync.forget(known.id) }
                pairing?.let { paired -> PairingStore(context).forget(paired.pc) }
                refresh()
            }
        }
        card.addView(remove, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT).apply { gravity = Gravity.END })
        return card
    }
}
