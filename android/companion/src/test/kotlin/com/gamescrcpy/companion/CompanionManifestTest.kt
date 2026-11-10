package com.gamescrcpy.companion

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.DisplayName
import org.junit.jupiter.api.Test
import org.w3c.dom.Element

class CompanionManifestTest {

    private val androidNamespace = "http://schemas.android.com/apk/res/android"

    private fun manifest(): Element {
        val factory = DocumentBuilderFactory.newInstance()
        factory.isNamespaceAware = true
        return factory.newDocumentBuilder().parse(File("src/main/AndroidManifest.xml")).documentElement
    }

    private fun children(parent: Element, tag: String): List<Element> {
        val nodes = parent.getElementsByTagName(tag)
        return (0 until nodes.length).map { nodes.item(it) as Element }
    }

    private fun Element.androidValue(name: String): String = getAttributeNS(androidNamespace, name)

    private fun permissions(root: Element): Set<String> =
        children(root, "uses-permission").map { it.androidValue("name") }.toSet()

    @Test
    @DisplayName("CMP-003 the companion service runs in the foreground as a special use with its reason, as Android 14 requires")
    fun cmp003ForegroundServiceIsDeclaredTheWayAndroid14Requires() {
        val root = manifest()
        val asked = permissions(root)
        assertTrue("android.permission.FOREGROUND_SERVICE" in asked)
        assertTrue("android.permission.FOREGROUND_SERVICE_SPECIAL_USE" in asked)

        val service = children(root, "service").single { it.androidValue("name") == ".CompanionService" }
        val types = service.androidValue("foregroundServiceType").split('|').toSet()
        assertTrue("specialUse" in types)
        assertTrue("mediaProjection" in types)
        assertTrue("android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION" in asked)
        assertEquals("android.permission.DUMP", service.androidValue("permission"))
        val subtype = children(service, "property").single {
            it.androidValue("name") == "android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE"
        }
        assertTrue(subtype.androidValue("value").isNotBlank())
    }

    @Test
    @DisplayName("CMP-001 the companion asks to draw over other apps and to post its notification")
    fun cmp001OverlayAndNotificationPermissionsAreDeclared() {
        val asked = permissions(manifest())
        assertTrue("android.permission.SYSTEM_ALERT_WINDOW" in asked)
        assertTrue("android.permission.POST_NOTIFICATIONS" in asked)
    }
}
