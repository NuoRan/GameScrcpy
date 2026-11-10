package com.gamescrcpy.companion

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Test

class QrLinkTest {

    private fun parse(text: String): QrLink = QrLinks.parse(text) ?: throw AssertionError("not accepted: $text")

    @Test
    fun theComputersQrTextsParseIntoAddressTokenAndKey() {
        val qr = LanVectors.load().getJSONArray("qr")
        for (i in 0 until qr.length()) {
            val entry = qr.getJSONObject(i)
            val link = parse(entry.getString("url"))
            assertEquals(entry.getString("host"), link.host)
            assertEquals(entry.getInt("port"), link.port)
            assertEquals(entry.getString("token"), link.token)
            assertEquals(entry.getString("key"), Hex.encode(link.key))
            assertEquals(entry.getString("target"), "/c/" + link.token)
        }
    }

    @Test
    fun spacesAroundTheTextAndOtherFragmentFieldsAreTolerated() {
        val url = LanVectors.load().getJSONArray("qr").getJSONObject(0).getString("url")
        val hash = url.indexOf('#')
        val link = parse("  " + url.substring(0, hash + 1) + "v=2&" + url.substring(hash + 1) + "\n")
        assertEquals(27185, link.port)
    }

    @Test
    fun anythingElseIsNotOurCode() {
        val good = LanVectors.load().getJSONArray("qr").getJSONObject(0).getString("url")
        val key = good.substring(good.indexOf("#k=") + 3)
        val token = good.substring(good.indexOf("/c/") + 3, good.indexOf('#'))
        val rejected = listOf(
            "",
            "hello",
            good.replace("http://", "https://"),
            good.substring(0, good.indexOf('#')),
            good.replace("#k=", "#x="),
            "http://192.168.1.23:27185/c/$token#k=" + key.substring(1),
            "http://192.168.1.23:27185/c/$token#k=****",
            "http://192.168.1.23:27185/d/$token#k=$key",
            "http://192.168.1.23:27185/c/#k=$key",
            "http://192.168.1.23:27185/c/to+ken#k=$key",
            "http://192.168.1.23:27185/c/" + "t".repeat(LanCodec.MAX_TOKEN + 1) + "#k=$key",
            "http://192.168.1.23/c/$token#k=$key",
            "http://192.168.1.23:0/c/$token#k=$key",
            "http://192.168.1.23:65536/c/$token#k=$key",
            "http://:27185/c/$token#k=$key",
        )
        for (text in rejected) {
            assertNull(QrLinks.parse(text), text)
        }
    }
}
