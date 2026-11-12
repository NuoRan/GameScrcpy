package com.gamescrcpy.companion

import android.app.Activity
import android.content.Context
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import com.gamescrcpy.protocol.Enums.CompanionItem
import com.gamescrcpy.protocol.Enums.ComputerLink

interface Shell {
    val activity: Activity
    val ui: Ui
    val sync: SettingsSync
    val options: PhoneOptions

    fun open(page: Int)

    fun scan()

    fun connectNow()

    fun stopCompanion()

    fun ask(permission: String): Boolean
}

abstract class Page(protected val shell: Shell, title: Int, subtitle: Int) {
    protected val ui: Ui = shell.ui
    protected val context: Context = ui.context
    protected val body: LinearLayout = ui.column(Look.SPACE_MD)
    private val content: LinearLayout = ui.column(Look.SPACE_XL)

    val view: ScrollView = ScrollView(context).apply {
        isFillViewport = true
        isVerticalScrollBarEnabled = false
        addView(content, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
    }

    init {
        val header = ui.column(Look.SPACE_XS)
        header.addView(ui.text(Look.DISPLAY, ui.palette.textPrimary, title))
        header.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, subtitle))
        content.addView(header, wide())
        content.addView(body, wide())
        inset(0)
    }

    fun inset(top: Int) {
        val side = ui.px(Look.SPACE_XL)
        content.setPaddingRelative(side, top + ui.px(Look.SPACE_XXL), side, ui.px(Look.SPACE_XXL))
    }

    abstract fun refresh()

    protected fun wide(): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

    protected fun weighted(): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)

    companion object {
        const val HOME = 0
        const val COMPUTERS = 1
        const val FEATURES = 2
        const val PERMISSIONS = 3
        const val ABOUT = 4
    }
}

enum class Feature(
    val item: CompanionItem,
    val title: Int,
    val short: Int,
    val about: Int,
    val icon: Int,
    val hue: Int,
) {
    CURSOR(
        CompanionItem.CURSOR,
        R.string.feature_cursor,
        R.string.feature_cursor_short,
        R.string.feature_cursor_about,
        R.drawable.ic_cursor,
        Look.LIME,
    ),
    HINTS(
        CompanionItem.HINTS,
        R.string.feature_hints,
        R.string.feature_hints_short,
        R.string.feature_hints_about,
        R.drawable.ic_hints,
        Look.TEAL,
    ),
    KEYBOARD(
        CompanionItem.IME,
        R.string.feature_keyboard,
        R.string.feature_keyboard_short,
        R.string.feature_keyboard_about,
        R.drawable.ic_keyboard,
        Look.AZURE,
    ),
    AUTO_CONNECT(
        CompanionItem.AUTO_CONNECT,
        R.string.feature_auto_connect,
        R.string.feature_auto_connect_short,
        R.string.feature_auto_connect_about,
        R.drawable.ic_wifi,
        Look.AMBER,
    ),
}

object Labels {
    fun link(computer: KnownComputer?, overLan: Boolean): Int = when {
        computer?.link == ComputerLink.USB -> R.string.link_usb
        computer?.link == ComputerLink.WIRELESS -> R.string.link_wireless
        computer?.link == ComputerLink.LAN || overLan -> R.string.link_lan
        else -> R.string.link_adb
    }

    fun name(context: Context, computer: KnownComputer?): String =
        computer?.name?.takeIf { it.isNotBlank() } ?: context.getString(R.string.computer_unnamed)
}
