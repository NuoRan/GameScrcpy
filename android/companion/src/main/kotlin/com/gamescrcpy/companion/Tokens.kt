package com.gamescrcpy.companion

object Tokens {
    const val SIZE = 16

    fun parse(text: String?): ByteArray? {
        if (text == null || text.length != SIZE * 2) {
            return null
        }
        val bytes = ByteArray(SIZE)
        for (i in 0 until SIZE) {
            val high = nibble(text[2 * i])
            val low = nibble(text[2 * i + 1])
            if (high < 0 || low < 0) {
                return null
            }
            bytes[i] = ((high shl 4) or low).toByte()
        }
        return bytes
    }

    fun matches(expected: ByteArray?, presented: ByteArray): Boolean {
        if (expected == null || expected.size != SIZE || presented.size != expected.size) {
            return false
        }
        var difference = 0
        for (i in expected.indices) {
            difference = difference or (expected[i].toInt() xor presented[i].toInt())
        }
        return difference == 0
    }

    private fun nibble(c: Char): Int = when (c) {
        in '0'..'9' -> c - '0'
        in 'a'..'f' -> c - 'a' + 10
        in 'A'..'F' -> c - 'A' + 10
        else -> -1
    }
}
