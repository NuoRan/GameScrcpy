package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Direction
import com.gamescrcpy.protocol.FrameContext
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class LanCryptoTest {

    @Test
    fun theSharedGcmKnownAnswersMatchTheJavaCipher() {
        val gcm = LanVectors.load().getJSONArray("gcm")
        for (i in 0 until gcm.length()) {
            val known = gcm.getJSONObject(i)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(
                Cipher.ENCRYPT_MODE,
                SecretKeySpec(LanVectors.bytes(known.getString("key")), "AES"),
                GCMParameterSpec(LanCodec.TAG_SIZE * 8, LanVectors.bytes(known.getString("nonce"))),
            )
            cipher.updateAAD(LanVectors.bytes(known.getString("aad")))
            val sealed = cipher.doFinal(LanVectors.bytes(known.getString("plaintext")))
            assertEquals(known.getString("ciphertext") + known.getString("tag"), Hex.encode(sealed), known.getString("name"))
        }
    }

    @Test
    fun handshakesSessionKeysAndRecordsMatchTheVectors() {
        val root = LanVectors.load()
        val key = LanVectors.bytes(root.getString("key"))
        val phone = LanVectors.bytes(root.getString("phone"))
        val pc = LanVectors.bytes(root.getString("pc"))
        assertEquals(LanCodec.CONTEXT, root.getString("context"))
        val handshakes = root.getJSONArray("handshakes")
        var accepted = 0
        for (i in 0 until handshakes.length()) {
            val shake = handshakes.getJSONObject(i)
            val name = shake.getString("name")
            val hello = LanVectors.bytes(shake.getString("hello"))
            val reply = LanVectors.bytes(shake.getString("reply"))
            val parsedHello = LanCodec.parseHello(hello)
            assertEquals(shake.getString("token"), parsedHello.token, name)
            assertArrayEquals(phone, parsedHello.phone, name)
            assertEquals(hello.size, LanCodec.helloSize(hello.copyOf(LanCodec.HEAD_SIZE)), name)
            assertEquals(LanCodec.REPLY_SIZE, reply.size, name)
            val parsedReply = LanCodec.parseReply(reply)
            if (shake.getString("status") != "accepted") {
                assertEquals(LanStatus.UNKNOWN, parsedReply.status, name)
                continue
            }
            accepted += 1
            assertEquals(LanStatus.ACCEPTED, parsedReply.status, name)
            assertArrayEquals(pc, parsedReply.pc, name)
            assertArrayEquals(hello, LanCodec.hello(parsedHello.token, phone, LanVectors.bytes(shake.getString("phoneNonce"))))
            assertArrayEquals(reply, LanCodec.reply(LanStatus.ACCEPTED, pc, LanVectors.bytes(shake.getString("pcNonce"))))
            val session = LanCodec.sessionKey(key, hello, reply)
            assertEquals(shake.getString("sessionKey"), Hex.encode(session), name)
            val sealers = mapOf(
                "toCompanion" to RecordSealer(session, LanCodec.TO_COMPANION),
                "toComputer" to RecordSealer(session, LanCodec.TO_COMPUTER),
            )
            val openers = mapOf(
                "toCompanion" to RecordOpener(session, LanCodec.TO_COMPANION),
                "toComputer" to RecordOpener(session, LanCodec.TO_COMPUTER),
            )
            val records = shake.getJSONArray("records")
            for (j in 0 until records.length()) {
                val record = records.getJSONObject(j)
                val label = name + " " + record.getString("message")
                val direction = record.getString("direction")
                val frame = LanVectors.bytes(record.getString("frame"))
                val sealed = LanVectors.bytes(record.getString("hex"))
                assertEquals(record.getString("hex"), Hex.encode(sealers.getValue(direction).seal(frame)), label)
                assertArrayEquals(frame, openers.getValue(direction).open(sealed.copyOf(4), sealed.copyOfRange(4, sealed.size)), label)
                val message = Codec.decodeFrame(
                    FrameContext.COMPANION,
                    if (direction == "toCompanion") Direction.TO_SERVER else Direction.TO_CLIENT,
                    frame,
                    0,
                    frame.size,
                )
                assertEquals(record.getString("message"), message.type().specName(), label)
            }
        }
        assertEquals(2, accepted)
    }

    @Test
    fun malformedHellosRepliesAndRecordsFailWithTheSpecError() {
        val root = LanVectors.load()
        val hellos = root.getJSONArray("malformedHellos")
        for (i in 0 until hellos.length()) {
            val entry = hellos.getJSONObject(i)
            val error = assertThrows(LanException::class.java) { LanCodec.parseHello(LanVectors.bytes(entry.getString("hex"))) }
            assertEquals(entry.getString("error"), error.error.specName, entry.getString("name"))
        }
        val replies = root.getJSONArray("malformedReplies")
        for (i in 0 until replies.length()) {
            val entry = replies.getJSONObject(i)
            val error = assertThrows(LanException::class.java) { LanCodec.parseReply(LanVectors.bytes(entry.getString("hex"))) }
            assertEquals(entry.getString("error"), error.error.specName, entry.getString("name"))
        }
        val session = LanVectors.bytes(root.getJSONArray("handshakes").getJSONObject(0).getString("sessionKey"))
        val records = root.getJSONArray("malformedRecords")
        for (i in 0 until records.length()) {
            val entry = records.getJSONObject(i)
            val direction = if (entry.getString("direction") == "toCompanion") LanCodec.TO_COMPANION else LanCodec.TO_COMPUTER
            val opener = RecordOpener(session, direction, entry.getLong("counter"))
            val bytes = LanVectors.bytes(entry.getString("hex"))
            val error = assertThrows(LanException::class.java) { opener.open(bytes.copyOf(4), bytes.copyOfRange(4, bytes.size)) }
            assertEquals(entry.getString("error"), error.error.specName, entry.getString("name"))
        }
    }

    @Test
    fun recordsRoundTripInOrderAndAReplayedOrMisdirectedRecordFails() {
        val random = SecureRandom()
        val session = ByteArray(LanCodec.KEY_SIZE).also { random.nextBytes(it) }
        val sealer = RecordSealer(session, LanCodec.TO_COMPUTER)
        val opener = RecordOpener(session, LanCodec.TO_COMPUTER)
        val sealed = (0 until 20).map { index ->
            val frame = ByteArray(6 + random.nextInt(300) + index).also { random.nextBytes(it) }
            frame to sealer.seal(frame)
        }
        for ((frame, record) in sealed) {
            assertEquals(frame.size + LanCodec.TAG_SIZE, LanCodec.recordLength(record.copyOf(4)))
            assertArrayEquals(frame, opener.open(record.copyOf(4), record.copyOfRange(4, record.size)))
        }
        val replay = sealed.last().second
        assertThrows(LanException::class.java) { opener.open(replay.copyOf(4), replay.copyOfRange(4, replay.size)) }
        val first = sealed.first().second
        val wrongWay = RecordOpener(session, LanCodec.TO_COMPANION)
        assertThrows(LanException::class.java) { wrongWay.open(first.copyOf(4), first.copyOfRange(4, first.size)) }
        val largest = ByteArray(LanCodec.MAX_RECORD - LanCodec.TAG_SIZE)
        val big = RecordSealer(session, LanCodec.TO_COMPANION).seal(largest)
        assertArrayEquals(largest, RecordOpener(session, LanCodec.TO_COMPANION).open(big.copyOf(4), big.copyOfRange(4, big.size)))
    }

    @Test
    fun theHelloCarriesAtMostSixtyFourTokenBytes() {
        val phone = ByteArray(LanCodec.ID_SIZE)
        val nonce = ByteArray(LanCodec.NONCE_SIZE)
        val hello = LanCodec.hello("a".repeat(LanCodec.MAX_TOKEN), phone, nonce)
        assertEquals(LanCodec.HEAD_SIZE + LanCodec.MAX_TOKEN + LanCodec.ID_SIZE + LanCodec.NONCE_SIZE, hello.size)
        assertThrows(IllegalArgumentException::class.java) { LanCodec.hello("a".repeat(LanCodec.MAX_TOKEN + 1), phone, nonce) }
    }
}
