package com.gamescrcpy.companion

import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Test

class PairingsTest {
    private val key = "ab".repeat(LanCodec.KEY_SIZE)

    private fun pairing(index: Int, host: String = "192.168.1.$index") =
        Pairing(Hex.encode(ByteArray(LanCodec.ID_SIZE) { index.toByte() }), key, host, 27185)

    @Test
    fun pairingsSurviveEncoding() {
        val pairings = listOf(pairing(1), pairing(2, "10.0.0.7"))
        assertEquals(pairings, Pairings.decode(Pairings.encode(pairings)))
        assertEquals(emptyList<Pairing>(), Pairings.decode(null))
        assertEquals(emptyList<Pairing>(), Pairings.decode(""))
        assertArrayEquals(ByteArray(LanCodec.KEY_SIZE) { 0xAB.toByte() }, pairings[0].keyBytes)
        assertArrayEquals(ByteArray(LanCodec.ID_SIZE) { 1 }, pairings[0].pcBytes)
    }

    @Test
    fun damagedLinesAreSkipped() {
        val good = pairing(3)
        val lines = listOf(
            "${good.pc},${good.key},${good.host},${good.port}",
            "zz${good.pc.substring(2)},${good.key},host,1",
            "${good.pc.substring(2)},${good.key},host,1",
            "${good.pc},${good.key.substring(2)},host,1",
            "${good.pc},${good.key},,1",
            "${good.pc},${good.key},host,port",
            "${good.pc},${good.key},host",
            "${good.pc},${good.key},host,1,extra",
        )
        assertEquals(listOf(good), Pairings.decode(lines.joinToString("\n")))
    }

    @Test
    fun theLatestComputerComesFirstAndAtMostFourAreKept() {
        var pairings = emptyList<Pairing>()
        for (index in 1..6) {
            pairings = Pairings.remember(pairings, pairing(index))
        }
        assertEquals(listOf(6, 5, 4, 3).map { pairing(it) }, pairings)
        val moved = pairing(4, "192.168.9.9")
        pairings = Pairings.remember(pairings, moved)
        assertEquals(listOf(moved, pairing(6), pairing(5), pairing(3)), pairings)
    }

    @Test
    fun hexRejectsOddLengthsAndForeignCharacters() {
        assertEquals("00ff7f80", Hex.encode(byteArrayOf(0, -1, 0x7F, -128)))
        assertArrayEquals(byteArrayOf(0, -1, 0x7F, -128), Hex.decode("00FF7f80"))
        assertNull(Hex.decode("abc"))
        assertNull(Hex.decode("zz"))
    }
}
