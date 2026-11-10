package com.gamescrcpy.companion

import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class TokensTest {

    @Test
    fun parsesThirtyTwoHexDigitsInEitherCase() {
        val expected = ByteArray(Tokens.SIZE) { (it * 0x11).toByte() }
        assertArrayEquals(expected, Tokens.parse("00112233445566778899aabbccddeeff"))
        assertArrayEquals(expected, Tokens.parse("00112233445566778899AABBCCDDEEFF"))
    }

    @Test
    fun refusesAnythingElse() {
        assertNull(Tokens.parse(null))
        assertNull(Tokens.parse(""))
        assertNull(Tokens.parse("00112233445566778899aabbccddeef"))
        assertNull(Tokens.parse("00112233445566778899aabbccddeeff0"))
        assertNull(Tokens.parse("00112233445566778899aabbccddeefg"))
        assertNull(Tokens.parse(" 0112233445566778899aabbccddeeff"))
        assertNull(Tokens.parse("0x112233445566778899aabbccddeeff"))
    }

    @Test
    fun matchesOnlyTheSameSixteenBytes() {
        val token = Tokens.parse("00112233445566778899aabbccddeeff")
        assertNotNull(token)
        val expected = token!!
        assertTrue(Tokens.matches(expected, expected.copyOf()))
        assertFalse(Tokens.matches(expected, expected.copyOf().also { it[15] = 0 }))
        assertFalse(Tokens.matches(expected, expected.copyOf().also { it[0] = 1 }))
        assertFalse(Tokens.matches(expected, expected.copyOf(15)))
        assertFalse(Tokens.matches(expected, ByteArray(0)))
        assertFalse(Tokens.matches(null, expected))
        assertFalse(Tokens.matches(ByteArray(15), ByteArray(15)))
    }
}
