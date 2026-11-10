package com.gamescrcpy.companion

import android.content.Context
import java.security.SecureRandom

data class Pairing(val pc: String, val key: String, val host: String, val port: Int) {
    val keyBytes: ByteArray
        get() = Hex.decode(key) ?: ByteArray(0)

    val pcBytes: ByteArray
        get() = Hex.decode(pc) ?: ByteArray(0)
}

object Hex {
    private const val DIGITS = "0123456789abcdef"

    fun encode(bytes: ByteArray): String {
        val text = StringBuilder(bytes.size * 2)
        for (byte in bytes) {
            val value = byte.toInt() and 0xFF
            text.append(DIGITS[value shr 4]).append(DIGITS[value and 0x0F])
        }
        return text.toString()
    }

    fun decode(text: String): ByteArray? {
        if (text.length % 2 != 0) {
            return null
        }
        val bytes = ByteArray(text.length / 2)
        for (i in bytes.indices) {
            val high = Character.digit(text[2 * i], 16)
            val low = Character.digit(text[2 * i + 1], 16)
            if (high < 0 || low < 0) {
                return null
            }
            bytes[i] = ((high shl 4) or low).toByte()
        }
        return bytes
    }
}

object Pairings {
    const val MOST = 4
    private const val FIELDS = 4

    fun encode(pairings: List<Pairing>): String =
        pairings.joinToString("\n") { "${it.pc},${it.key},${it.host},${it.port}" }

    fun decode(text: String?): List<Pairing> {
        if (text.isNullOrEmpty()) {
            return emptyList()
        }
        return text.split('\n').mapNotNull { line ->
            val fields = line.split(',')
            val port = fields.getOrNull(3)?.toIntOrNull()
            if (fields.size != FIELDS || port == null || !valid(fields[0], LanCodec.ID_SIZE) ||
                !valid(fields[1], LanCodec.KEY_SIZE) || fields[2].isEmpty()
            ) {
                null
            } else {
                Pairing(fields[0], fields[1], fields[2], port)
            }
        }
    }

    fun remember(pairings: List<Pairing>, latest: Pairing): List<Pairing> =
        (listOf(latest) + pairings.filter { it.pc != latest.pc }).take(MOST)

    private fun valid(hex: String, size: Int): Boolean = Hex.decode(hex)?.size == size
}

class PairingStore(context: Context) : PairingSource {
    private val preferences = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    override val phone: ByteArray
        @Synchronized get() {
            Hex.decode(preferences.getString(PHONE, null) ?: "")?.takeIf { it.size == LanCodec.ID_SIZE }?.let {
                return it
            }
            val created = ByteArray(LanCodec.ID_SIZE).also { SecureRandom().nextBytes(it) }
            preferences.edit().putString(PHONE, Hex.encode(created)).apply()
            return created
        }

    @Synchronized
    override fun all(): List<Pairing> = Pairings.decode(preferences.getString(PAIRINGS, null))

    @Synchronized
    override fun remember(latest: Pairing) {
        save(Pairings.remember(all(), latest))
    }

    private fun save(pairings: List<Pairing>) {
        preferences.edit().putString(PAIRINGS, Pairings.encode(pairings)).apply()
    }

    private companion object {
        const val FILE = "lan"
        const val PHONE = "phone"
        const val PAIRINGS = "pairings"
    }
}
