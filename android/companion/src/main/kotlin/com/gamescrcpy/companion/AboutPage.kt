package com.gamescrcpy.companion

import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build

class AboutPage(shell: Shell) : Page(shell, R.string.about_title, R.string.about_subtitle) {
    private val version = ui.text(Look.CAPTION, ui.palette.textTertiary)

    init {
        val intro = ui.card(gap = Look.SPACE_MD)
        val head = ui.row(Look.SPACE_MD)
        head.addView(ui.tile(R.drawable.ic_nav_about, Look.EMBER, large = true))
        val names = ui.column(Look.SPACE_XXS)
        names.addView(ui.text(Look.SUBTITLE, ui.palette.textPrimary, R.string.app_name))
        names.addView(version)
        head.addView(names, weighted())
        intro.addView(head, wide())
        intro.addView(ui.text(Look.BODY, ui.palette.textSecondary, R.string.about_text))
        val help = ui.button(ButtonKind.SECONDARY, R.string.about_help, R.drawable.ic_nav_about)
        help.setOnClickListener { browse(context.getString(R.string.about_help_link)) }
        intro.addView(help, wide())
        body.addView(intro, wide())

        val privacy = ui.card(gap = Look.SPACE_SM)
        privacy.addView(ui.text(Look.HEADING, ui.palette.textPrimary, R.string.about_privacy))
        privacy.addView(ui.text(Look.BODY, ui.palette.textSecondary, R.string.about_privacy_text))
        body.addView(privacy, wide())

        val licenses = ui.card(gap = Look.SPACE_SM)
        licenses.addView(ui.text(Look.HEADING, ui.palette.textPrimary, R.string.about_licenses))
        licenses.addView(ui.text(Look.CAPTION, ui.palette.textTertiary, R.string.about_licenses_text))
        for ((name, license) in LICENSES) {
            licenses.addView(ui.divider())
            val line = ui.column(Look.SPACE_XXS)
            line.addView(ui.text(Look.LABEL, ui.palette.textPrimary, name))
            line.addView(ui.text(Look.CAPTION, ui.palette.textSecondary, license))
            licenses.addView(line, wide())
        }
        body.addView(licenses, wide())
    }

    override fun refresh() {
        val info = packageInfo() ?: return
        version.text = context.getString(R.string.about_version, info.versionName.orEmpty(), info.longVersionCode)
    }

    private fun packageInfo(): PackageInfo? = try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
        } else {
            legacyPackageInfo()
        }
    } catch (e: PackageManager.NameNotFoundException) {
        null
    }

    @Suppress("DEPRECATION")
    private fun legacyPackageInfo(): PackageInfo = context.packageManager.getPackageInfo(context.packageName, 0)

    private fun browse(link: String) {
        try {
            shell.activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(link)))
        } catch (e: ActivityNotFoundException) {
            return
        }
    }

    private companion object {
        val LICENSES = listOf(
            R.string.license_gamescrcpy to R.string.license_apache,
            R.string.license_kotlin to R.string.license_apache,
            R.string.license_camerax to R.string.license_apache,
            R.string.license_lifecycle to R.string.license_apache,
            R.string.license_zxing to R.string.license_apache,
        )
    }
}
