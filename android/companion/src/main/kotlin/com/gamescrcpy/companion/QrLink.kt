package com.gamescrcpy.companion

import java.util.Base64

class QrLink(val host: String, val port: Int, val token: String, val key: ByteArray)

object QrLinks {
    private const val SCHEME = "http://"
    private const val PATH = "/c/"
    private const val KEY_FIELD = "k="
    private const val MAX_PORT = 65535

    fun parse(text: String): QrLink? {
        val trimmed = text.trim()
        if (!trimmed.startsWith(SCHEME)) {
            return null
        }
        val hash = trimmed.indexOf('#')
        if (hash < 0) {
            return null
        }
        val location = trimmed.substring(SCHEME.length, hash)
        val slash = location.indexOf('/')
        if (slash <= 0) {
            return null
        }
        val authority = location.substring(0, slash)
        val path = location.substring(slash)
        if (!path.startsWith(PATH)) {
            return null
        }
        val token = path.substring(PATH.length)
        if (token.isEmpty() || token.length > LanCodec.MAX_TOKEN || !token.all(::isTokenCharacter)) {
            return null
        }
        val colon = authority.lastIndexOf(':')
        if (colon <= 0) {
            return null
        }
        val host = authority.substring(0, colon)
        val port = authority.substring(colon + 1).toIntOrNull()?.takeIf { it in 1..MAX_PORT } ?: return null
        val key = trimmed.substring(hash + 1).split('&')
            .firstOrNull { it.startsWith(KEY_FIELD) }
            ?.let { decodeKey(it.substring(KEY_FIELD.length)) }
            ?: return null
        return QrLink(host, port, token, key)
    }

    private fun decodeKey(text: String): ByteArray? {
        val key = try {
            Base64.getUrlDecoder().decode(text)
        } catch (e: IllegalArgumentException) {
            return null
        }
        return key.takeIf { it.size == LanCodec.KEY_SIZE }
    }

    private fun isTokenCharacter(c: Char): Boolean =
        c in 'A'..'Z' || c in 'a'..'z' || c in '0'..'9' || c == '-' || c == '_'
}
