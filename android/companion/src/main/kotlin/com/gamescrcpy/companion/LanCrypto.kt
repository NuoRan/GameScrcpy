package com.gamescrcpy.companion

import java.io.IOException
import java.nio.ByteBuffer
import java.security.GeneralSecurityException
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.Mac
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

enum class LanError(val specName: String) {
    TRUNCATED("Truncated"),
    BAD_MAGIC("BadMagic"),
    VERSION_MISMATCH("VersionMismatch"),
    LENGTH_EXCEEDED("LengthExceeded"),
    INVALID_ENUM("InvalidEnum"),
    OUT_OF_RANGE("OutOfRange"),
    BAD_TAG("BadTag"),
}

class LanException(val error: LanError) : IOException(error.specName)

enum class LanStatus(val wire: Int) {
    ACCEPTED(0),
    UNKNOWN(1),
    VERSION_MISMATCH(2),
}

class LanHello(val token: String, val phone: ByteArray, val nonce: ByteArray)

class LanReply(val status: LanStatus, val pc: ByteArray, val nonce: ByteArray)

object LanCodec {
    const val VERSION = 1
    const val KEY_SIZE = 32
    const val ID_SIZE = 16
    const val NONCE_SIZE = 16
    const val MAX_TOKEN = 64
    const val HEAD_SIZE = 6
    const val REPLY_SIZE = HEAD_SIZE + ID_SIZE + NONCE_SIZE
    const val TAG_SIZE = 16
    const val TO_COMPANION = 0
    const val TO_COMPUTER = 1
    const val MIN_RECORD = 6 + TAG_SIZE
    const val MAX_RECORD = 6 + 16 * 1024 + TAG_SIZE
    const val CONTEXT = "GSCP/1 companion lan"
    private val MAGIC = byteArrayOf('G'.code.toByte(), 'S'.code.toByte(), 'C'.code.toByte(), 'L'.code.toByte())

    fun hello(token: String, phone: ByteArray, nonce: ByteArray): ByteArray {
        val text = token.toByteArray(Charsets.US_ASCII)
        require(text.size <= MAX_TOKEN && phone.size == ID_SIZE && nonce.size == NONCE_SIZE)
        return MAGIC + byteArrayOf(VERSION.toByte(), text.size.toByte()) + text + phone + nonce
    }

    fun helloSize(head: ByteArray): Int {
        val length = checkHead(head, HEAD_SIZE)
        if (length > MAX_TOKEN) {
            throw LanException(LanError.LENGTH_EXCEEDED)
        }
        return HEAD_SIZE + length + ID_SIZE + NONCE_SIZE
    }

    fun parseHello(bytes: ByteArray): LanHello {
        val size = helloSize(bytes)
        if (bytes.size < size) {
            throw LanException(LanError.TRUNCATED)
        }
        val token = size - HEAD_SIZE - ID_SIZE - NONCE_SIZE
        val text = String(bytes, HEAD_SIZE, token, Charsets.US_ASCII)
        val phone = bytes.copyOfRange(HEAD_SIZE + token, HEAD_SIZE + token + ID_SIZE)
        val nonce = bytes.copyOfRange(HEAD_SIZE + token + ID_SIZE, size)
        return LanHello(text, phone, nonce)
    }

    fun reply(status: LanStatus, pc: ByteArray, nonce: ByteArray): ByteArray {
        require(pc.size == ID_SIZE && nonce.size == NONCE_SIZE)
        return MAGIC + byteArrayOf(VERSION.toByte(), status.wire.toByte()) + pc + nonce
    }

    fun parseReply(bytes: ByteArray): LanReply {
        val wire = checkHead(bytes, REPLY_SIZE)
        val status = LanStatus.entries.firstOrNull { it.wire == wire } ?: throw LanException(LanError.INVALID_ENUM)
        val pc = bytes.copyOfRange(HEAD_SIZE, HEAD_SIZE + ID_SIZE)
        val nonce = bytes.copyOfRange(HEAD_SIZE + ID_SIZE, REPLY_SIZE)
        return LanReply(status, pc, nonce)
    }

    fun sessionKey(key: ByteArray, hello: ByteArray, reply: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        mac.update(CONTEXT.toByteArray(Charsets.US_ASCII))
        mac.update(hello)
        mac.update(reply)
        return mac.doFinal()
    }

    fun recordLength(prefix: ByteArray): Int {
        if (prefix.size != 4) {
            throw LanException(LanError.TRUNCATED)
        }
        val length = ByteBuffer.wrap(prefix).int.toLong() and 0xFFFFFFFFL
        if (length < MIN_RECORD || length > MAX_RECORD) {
            throw LanException(LanError.OUT_OF_RANGE)
        }
        return length.toInt()
    }

    internal fun nonce(direction: Int, counter: Long): ByteArray =
        ByteBuffer.allocate(12).putInt(direction).putLong(counter).array()

    private fun checkHead(bytes: ByteArray, size: Int): Int {
        if (bytes.size < size) {
            throw LanException(LanError.TRUNCATED)
        }
        for (i in MAGIC.indices) {
            if (bytes[i] != MAGIC[i]) {
                throw LanException(LanError.BAD_MAGIC)
            }
        }
        if (bytes[4].toInt() != VERSION) {
            throw LanException(LanError.VERSION_MISMATCH)
        }
        return bytes[5].toInt() and 0xFF
    }
}

class RecordSealer(session: ByteArray, private val direction: Int, private var counter: Long = 0) {
    private val key = SecretKeySpec(session, "AES")

    fun seal(frame: ByteArray): ByteArray {
        val prefix = ByteBuffer.allocate(4).putInt(frame.size + LanCodec.TAG_SIZE).array()
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key, GCMParameterSpec(LanCodec.TAG_SIZE * 8, LanCodec.nonce(direction, counter)))
        counter += 1
        cipher.updateAAD(prefix)
        return prefix + cipher.doFinal(frame)
    }
}

class RecordOpener(session: ByteArray, private val direction: Int, private var counter: Long = 0) {
    private val key = SecretKeySpec(session, "AES")

    fun open(prefix: ByteArray, body: ByteArray): ByteArray {
        val length = LanCodec.recordLength(prefix)
        if (body.size != length) {
            throw LanException(LanError.TRUNCATED)
        }
        val plain = try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(LanCodec.TAG_SIZE * 8, LanCodec.nonce(direction, counter)))
            cipher.updateAAD(prefix)
            cipher.doFinal(body)
        } catch (e: AEADBadTagException) {
            throw LanException(LanError.BAD_TAG)
        } catch (e: GeneralSecurityException) {
            throw LanException(LanError.BAD_TAG)
        }
        counter += 1
        return plain
    }
}
