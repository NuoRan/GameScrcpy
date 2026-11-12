package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Direction
import com.gamescrcpy.protocol.FrameContext
import com.gamescrcpy.protocol.Message
import com.gamescrcpy.protocol.Messages
import java.io.DataInputStream
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.SecureRandom
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertInstanceOf
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.DisplayName
import org.junit.jupiter.api.Test

class LanConnectorTest {
    private val key = ByteArray(LanCodec.KEY_SIZE) { (it * 7 + 1).toByte() }
    private val pc = ByteArray(LanCodec.ID_SIZE) { (0xC0 + it).toByte() }
    private val timing = LanConnector.Timing(
        connectMillis = 1_000,
        handshakeMillis = 2_000,
        discoverMillis = 50,
        firstBackoffMillis = 50,
        mostBackoffMillis = 200,
        stableMillis = 60_000,
    )

    @Test
    fun scanningPairsRemembersTheComputerAndCarriesEncryptedFrames() {
        FakeComputer(key, pc).use { computer ->
            val store = MemoryPairings()
            val events = Recorder()
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, events, timing).use { connector ->
                connector.start(QrLink("127.0.0.1", computer.port, "tok_en-1", key))
                val connection = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                val visit = computer.next()
                assertEquals("tok_en-1", visit.hello.token)
                assertArrayEquals(store.phone, visit.hello.phone)
                val expected = Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)
                assertEquals(listOf(expected), store.all())
                assertEquals(expected, events.pairings.poll(5, TimeUnit.SECONDS))
                assertTrue(events.reaches(LanState.CONNECTED))
                assertTrue(connection.authenticated)

                val welcome = Codec.encodeFrame(Messages.CompanionWelcome())
                connection.output.write(welcome)
                connection.output.flush()
                assertArrayEquals(welcome, visit.receive())

                val commit = Codec.encodeFrame(Messages.CommitText().also { it.text = "你好" })
                visit.send(commit)
                val read = ByteArray(commit.size)
                DataInputStream(connection.input).readFully(read)
                assertArrayEquals(commit, read)
            }
        }
    }

    @Test
    fun aRememberedComputerIsJoinedAgainButAnotherComputerAtThatAddressIsNot() {
        FakeComputer(key, pc).use { computer ->
            val store = MemoryPairings(listOf(Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)))
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, Recorder(), timing).use { connector ->
                connector.start(null)
                val first = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                assertEquals("", computer.next().hello.token)
                computer.id = ByteArray(LanCodec.ID_SIZE) { 0x11 }
                first.close()
                computer.next()
                assertNull(delivered.poll(400, TimeUnit.MILLISECONDS))
                computer.id = pc
                connector.nudge()
                val second = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("the computer was not joined again")
                second.close()
                assertEquals(1, store.all().size)
            }
        }
    }

    @Test
    fun anUnknownAnswerKeepsThePairingAndTriesAgain() {
        FakeComputer(key, pc).use { computer ->
            val answers = AtomicInteger()
            computer.answer = { if (answers.getAndIncrement() == 0) LanStatus.UNKNOWN else LanStatus.ACCEPTED }
            val pairing = Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)
            val store = MemoryPairings(listOf(pairing))
            val events = Recorder()
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, events, timing).use { connector ->
                connector.start(null)
                val connection = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                assertTrue(answers.get() >= 2)
                assertEquals(listOf(pairing), store.all())
                assertTrue(events.reaches(LanState.CONNECTED))
                connection.close()
            }
        }
    }

    @Test
    fun aScanThatFailsWithNothingPairedGivesUp() {
        FakeComputer(key, pc).use { computer ->
            computer.answer = { LanStatus.UNKNOWN }
            val store = MemoryPairings()
            val events = Recorder()
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, events, timing).use { connector ->
                connector.start(QrLink("127.0.0.1", computer.port, "stale", key))
                assertTrue(events.reaches(LanState.FAILED))
                assertNull(delivered.poll(300, TimeUnit.MILLISECONDS))
                assertEquals(emptyList<Pairing>(), store.all())
            }
        }
    }

    @Test
    fun aComputerThatMovedIsFoundByItsName() {
        FakeComputer(key, pc).use { computer ->
            val gone = ServerSocket(0, 1, InetAddress.getLoopbackAddress()).use { it.localPort }
            val store = MemoryPairings(listOf(Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", gone)))
            val asked = LinkedBlockingQueue<Set<String>>()
            val discovery = Discovery { names, _ ->
                asked.put(names)
                mapOf("gs-" + Hex.encode(pc) to LanAddress("127.0.0.1", computer.port))
            }
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, discovery, { delivered.put(it) }, Recorder(), timing).use { connector ->
                connector.start(null)
                val connection = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                assertEquals(setOf("gs-" + Hex.encode(pc)), asked.poll(1, TimeUnit.SECONDS))
                assertEquals(listOf(Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)), store.all())
                connection.close()
            }
        }
    }

    @Test
    fun theCompanionServerTalksToTheComputerOverTheEncryptedLink() {
        FakeComputer(key, pc).use { computer ->
            val store = MemoryPairings()
            val queue = QueueListener()
            val events = ServerRecorder()
            val serverTiming = CompanionServer.Timing(idleMillis = 200, helloMillis = 2_000, silenceMillis = 4_000, drainMillis = 1_000)
            CompanionServer(queue, CursorSink { }, events, serverTiming, persistent = true).use { server ->
                server.start()
                LanConnector(store, Discovery { _, _ -> emptyMap() }, { queue.offer(it) }, Recorder(), timing).use { connector ->
                    connector.start(QrLink("127.0.0.1", computer.port, "token", key))
                    val first = computer.next()
                    first.sendMessage(hello())
                    val welcome = assertInstanceOf(Messages.CompanionWelcome::class.java, first.receiveMessage())
                    assertEquals(Constants.COMPANION_VERSION, welcome.version)
                    assertEquals(true, events.connections.poll(5, TimeUnit.SECONDS))
                    first.sendMessage(Messages.Ping().also { it.nonce = 3 })
                    assertEquals(3L, assertInstanceOf(Messages.Pong::class.java, first.receiveMessage()).nonce)
                    first.sendMessage(Messages.Goodbye())
                    assertEquals(false, events.connections.poll(5, TimeUnit.SECONDS))
                    assertFalse(server.ended)

                    val second = computer.next()
                    assertEquals("", second.hello.token)
                    second.sendMessage(hello())
                    assertInstanceOf(Messages.CompanionWelcome::class.java, second.receiveMessage())
                    assertEquals(true, events.connections.poll(5, TimeUnit.SECONDS))
                    assertNull(events.endings.poll(300, TimeUnit.MILLISECONDS))
                }
            }
        }
    }

    @Test
    fun anUnprovenLanConnectionDoesNotReplaceTheLiveOne() {
        FakeComputer(key, pc).use { computer ->
            val queue = QueueListener()
            val events = ServerRecorder()
            val serverTiming = CompanionServer.Timing(idleMillis = 200, helloMillis = 2_000, silenceMillis = 4_000, drainMillis = 1_000)
            CompanionServer(queue, CursorSink { }, events, serverTiming, persistent = true).use { server ->
                server.start()
                LanConnector(MemoryPairings(), Discovery { _, _ -> emptyMap() }, { queue.offer(it) }, Recorder(), timing).use { connector ->
                    connector.start(QrLink("127.0.0.1", computer.port, "token", key))
                    val live = computer.next()
                    live.sendMessage(hello())
                    assertInstanceOf(Messages.CompanionWelcome::class.java, live.receiveMessage())
                    assertEquals(true, events.connections.poll(5, TimeUnit.SECONDS))

                    ServerSocket(0, 1, InetAddress.getLoopbackAddress()).use { listening ->
                        Socket(InetAddress.getLoopbackAddress(), listening.localPort).use { phoneEnd ->
                            listening.accept().use { stranger ->
                                queue.offer(LanConnection(phoneEnd, ByteArray(LanCodec.KEY_SIZE) { 1 }))
                                val forged = RecordSealer(ByteArray(LanCodec.KEY_SIZE) { 2 }, LanCodec.TO_COMPANION)
                                stranger.getOutputStream().write(forged.seal(Codec.encodeFrame(hello())))
                                stranger.getOutputStream().flush()
                                stranger.soTimeout = 5_000
                                assertEquals(-1, stranger.getInputStream().read())
                            }
                        }
                    }
                    assertNull(events.connections.poll(300, TimeUnit.MILLISECONDS))
                    assertFalse(server.ended)
                    live.sendMessage(Messages.Ping().also { it.nonce = 5 })
                    assertEquals(5L, assertInstanceOf(Messages.Pong::class.java, live.receiveMessage()).nonce)
                }
            }
        }
    }

    @Test
    @DisplayName("ADR-0112: with auto connect off, a dropped connection is not joined again and a failed try is not repeated")
    fun withAutoConnectOffTheConnectorStopsAfterOneTry() {
        FakeComputer(key, pc).use { computer ->
            val store = MemoryPairings(listOf(Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)))
            val events = Recorder()
            val delivered = LinkedBlockingQueue<LanConnection>()
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, events, timing) { false }.use { connector ->
                connector.start(null)
                val first = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                assertEquals(Hex.encode(pc), first.pc)
                computer.next()
                first.close()
                assertTrue(events.reaches(LanState.IDLE))
                assertNull(delivered.poll(400, TimeUnit.MILLISECONDS))

                computer.answer = { LanStatus.UNKNOWN }
                connector.start(null)
                assertTrue(events.reaches(LanState.FAILED))
                assertNull(delivered.poll(400, TimeUnit.MILLISECONDS))
            }
        }
    }

    @Test
    @DisplayName("ADR-0112: once auto connect is back on, starting again joins the computer and joins it again after a drop")
    fun startingAgainOnceAutoConnectIsBackKeepsJoining() {
        FakeComputer(key, pc).use { computer ->
            val store = MemoryPairings(listOf(Pairing(Hex.encode(pc), Hex.encode(key), "127.0.0.1", computer.port)))
            val events = Recorder()
            val delivered = LinkedBlockingQueue<LanConnection>()
            val wanted = AtomicBoolean(false)
            LanConnector(store, Discovery { _, _ -> emptyMap() }, { delivered.put(it) }, events, timing) { wanted.get() }.use { connector ->
                connector.start(null)
                val first = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered")
                computer.next()
                first.close()
                assertTrue(events.reaches(LanState.IDLE))

                wanted.set(true)
                connector.start(null)
                val second = delivered.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("nothing was delivered again")
                computer.next()
                second.close()
                val third = delivered.poll(5, TimeUnit.SECONDS)
                    ?: throw AssertionError("the dropped connection was not joined again")
                assertEquals(Hex.encode(pc), third.pc)
                computer.next()
                third.close()
            }
        }
    }

    private fun hello() = Messages.CompanionHello().also {
        it.version = Constants.COMPANION_VERSION
        it.token = ByteArray(0)
    }

    private class MemoryPairings(initial: List<Pairing> = emptyList()) : PairingSource {
        private val list = ArrayList(initial)

        override val phone: ByteArray = ByteArray(LanCodec.ID_SIZE) { (0x50 + it).toByte() }

        @Synchronized
        override fun all(): List<Pairing> = list.toList()

        @Synchronized
        override fun remember(latest: Pairing) {
            val next = Pairings.remember(list, latest)
            list.clear()
            list.addAll(next)
        }
    }

    private class Recorder : LanConnector.Events {
        val states = LinkedBlockingQueue<LanState>()
        val pairings = LinkedBlockingQueue<Pairing>()

        override fun state(state: LanState) {
            states.put(state)
        }

        override fun paired(pairing: Pairing) {
            pairings.put(pairing)
        }

        fun reaches(wanted: LanState): Boolean {
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
            while (true) {
                val left = deadline - System.nanoTime()
                if (left <= 0) {
                    return false
                }
                if (states.poll(left, TimeUnit.NANOSECONDS) == wanted) {
                    return true
                }
            }
        }
    }

    private class ServerRecorder : CompanionServer.Events {
        val connections = LinkedBlockingQueue<Boolean>()
        val endings = LinkedBlockingQueue<CompanionServer.Ending>()

        override fun connectionChanged(connected: Boolean) {
            connections.put(connected)
        }

        override fun ended(ending: CompanionServer.Ending) {
            endings.put(ending)
        }
    }

    private class FakeComputer(private val key: ByteArray, pc: ByteArray) : AutoCloseable {
        private val server = ServerSocket(0, 8, InetAddress.getLoopbackAddress())
        private val visits = LinkedBlockingQueue<Visit>()
        private val opened = LinkedBlockingQueue<Visit>()
        private val random = SecureRandom()
        val port: Int = server.localPort

        @Volatile
        var id: ByteArray = pc

        @Volatile
        var answer: (LanHello) -> LanStatus = { LanStatus.ACCEPTED }

        init {
            thread(isDaemon = true, name = "fake-computer") { serve() }
        }

        fun next(): Visit = visits.poll(5, TimeUnit.SECONDS) ?: throw AssertionError("the companion did not come")

        override fun close() {
            server.close()
            opened.forEach { it.close() }
        }

        private fun serve() {
            while (true) {
                val socket = try {
                    server.accept()
                } catch (e: IOException) {
                    return
                }
                try {
                    socket.soTimeout = 5_000
                    val input = DataInputStream(socket.getInputStream())
                    val head = ByteArray(LanCodec.HEAD_SIZE)
                    input.readFully(head)
                    val rest = ByteArray(LanCodec.helloSize(head) - LanCodec.HEAD_SIZE)
                    input.readFully(rest)
                    val helloBytes = head + rest
                    val hello = LanCodec.parseHello(helloBytes)
                    val status = answer(hello)
                    val accepted = status == LanStatus.ACCEPTED
                    val nonce = ByteArray(LanCodec.NONCE_SIZE).also { random.nextBytes(it) }
                    val reply = LanCodec.reply(status, if (accepted) id else ByteArray(LanCodec.ID_SIZE), nonce)
                    socket.getOutputStream().write(reply)
                    socket.getOutputStream().flush()
                    val visit = Visit(socket, input, hello, if (accepted) LanCodec.sessionKey(key, helloBytes, reply) else null)
                    opened.put(visit)
                    if (!accepted) {
                        socket.close()
                    }
                    visits.put(visit)
                } catch (e: IOException) {
                    socket.close()
                }
            }
        }
    }

    private class Visit(
        private val socket: Socket,
        private val input: DataInputStream,
        val hello: LanHello,
        session: ByteArray?,
    ) : AutoCloseable {
        private val sealer = session?.let { RecordSealer(it, LanCodec.TO_COMPANION) }
        private val opener = session?.let { RecordOpener(it, LanCodec.TO_COMPUTER) }

        fun send(frame: ByteArray) {
            val seal = sealer ?: throw AssertionError("the computer refused this visit")
            socket.getOutputStream().write(seal.seal(frame))
            socket.getOutputStream().flush()
        }

        fun receive(): ByteArray {
            val open = opener ?: throw AssertionError("the computer refused this visit")
            val prefix = ByteArray(4)
            input.readFully(prefix)
            val body = ByteArray(LanCodec.recordLength(prefix))
            input.readFully(body)
            return open.open(prefix, body)
        }

        fun sendMessage(message: Message) {
            send(Codec.encodeFrame(message))
        }

        fun receiveMessage(): Message {
            val frame = receive()
            return Codec.decodeFrame(FrameContext.COMPANION, Direction.TO_CLIENT, frame, 0, frame.size)
        }

        override fun close() {
            socket.close()
        }
    }
}
