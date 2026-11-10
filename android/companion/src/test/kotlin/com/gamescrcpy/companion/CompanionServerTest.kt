package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Direction
import com.gamescrcpy.protocol.Enums.ErrorCode
import com.gamescrcpy.protocol.Enums.ImeAction
import com.gamescrcpy.protocol.Enums.KeyHintShape
import com.gamescrcpy.protocol.Enums.OverlayStatus
import com.gamescrcpy.protocol.Enums.ScreenshotProblem
import com.gamescrcpy.protocol.FrameContext
import com.gamescrcpy.protocol.Message
import com.gamescrcpy.protocol.Messages
import java.io.DataInputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertInstanceOf
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class CompanionServerTest {

    private val token = ByteArray(16) { (it + 1).toByte() }
    private val timing = CompanionServer.Timing(idleMillis = 10_000, helloMillis = 2_000, silenceMillis = 4_000,
        drainMillis = 2_000)

    @Test
    fun theRightTokenIsWelcomedAndCursorStatesReachTheOverlay() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing).use { server ->
            server.setToken(token)
            server.overlayChanged(OverlayInfo(granted = true, width = 1080, height = 2400, rotation = 0))
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                val welcome = assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                assertEquals(Constants.COMPANION_VERSION, welcome.version)
                assertEquals(OverlayStatus.GRANTED, welcome.overlay)
                assertEquals(1080, welcome.width)
                assertEquals(2400, welcome.height)
                assertEquals(0, welcome.rotation)
                assertEquals(true, recorder.connections.poll(5, TimeUnit.SECONDS))

                client.send(cursor(10, 20, Messages.CursorState.VISIBLE or Messages.CursorState.PRESSED))
                assertEquals(CursorSample(10, 20, visible = true, pressed = true), recorder.samples.poll(5, TimeUnit.SECONDS))
                client.send(Messages.Ping().also {
                    it.nonce = 7
                    it.clientTimeUs = 99
                })
                val pong = assertInstanceOf(Messages.Pong::class.java, client.read())
                assertEquals(7L, pong.nonce)
                assertEquals(99L, pong.clientTimeUs)

                server.overlayChanged(OverlayInfo(granted = true, width = 2400, height = 1080, rotation = 1))
                server.overlayChanged(OverlayInfo(granted = true, width = 2400, height = 1080, rotation = 1))
                server.overlayChanged(OverlayInfo(granted = false, width = 5, height = 5, rotation = 1))
                val rotated = assertInstanceOf(Messages.OverlayChanged::class.java, client.read())
                assertEquals(OverlayStatus.GRANTED, rotated.overlay)
                assertEquals(2400, rotated.width)
                assertEquals(1080, rotated.height)
                assertEquals(1, rotated.rotation)
                val revoked = assertInstanceOf(Messages.OverlayChanged::class.java, client.read())
                assertEquals(OverlayStatus.NO_PERMISSION, revoked.overlay)
                assertEquals(0, revoked.width)
                assertEquals(0, revoked.height)

                client.send(Messages.Goodbye())
                assertNull(client.read())
            }
            assertEquals(false, recorder.connections.poll(5, TimeUnit.SECONDS))
            assertEquals(CompanionServer.Ending.CLIENT_GOODBYE, recorder.endings.poll(5, TimeUnit.SECONDS))
            assertEquals(CursorSample.HIDDEN, recorder.samples.poll(5, TimeUnit.SECONDS))
            assertTrue(server.ended)
        }
    }

    @Test
    fun aWrongTokenIsRefusedWithAuthenticationFailed() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing).use { server ->
            server.setToken(token)
            server.start()
            for (presented in listOf(ByteArray(16), token.copyOf(15), ByteArray(0))) {
                Client(listener.port).use { client ->
                    client.send(hello(presented = presented))
                    val goodbye = assertInstanceOf(Messages.ServerGoodbye::class.java, client.read())
                    assertEquals(ErrorCode.AUTHENTICATION_FAILED, goodbye.reason)
                }
            }
            assertNull(recorder.connections.poll(300, TimeUnit.MILLISECONDS))
            assertTrue(recorder.samples.isEmpty())
        }
    }

    @Test
    fun anotherVersionOrAnotherFirstMessageIsRefused() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello(version = 2))
                val goodbye = assertInstanceOf(Messages.ServerGoodbye::class.java, client.read())
                assertEquals(ErrorCode.VERSION_MISMATCH, goodbye.reason)
                assertTrue(goodbye.message.contains("1") && goodbye.message.contains("2"))
            }
            Client(listener.port).use { client ->
                client.send(Messages.Ping())
                assertEquals(ErrorCode.UNEXPECTED_MESSAGE,
                    assertInstanceOf(Messages.ServerGoodbye::class.java, client.read()).reason)
            }
            Client(listener.port).use { client ->
                client.sendRaw(Codec.encodeFrame(Messages.CompanionWelcome()))
                assertEquals(ErrorCode.WRONG_DIRECTION,
                    assertInstanceOf(Messages.ServerGoodbye::class.java, client.read()).reason)
            }
            Client(listener.port).use { client ->
                client.sendRaw(byteArrayOf(0x01, 0x01, 0, 0, 0, 0))
                assertEquals(ErrorCode.UNEXPECTED_MESSAGE,
                    assertInstanceOf(Messages.ServerGoodbye::class.java, client.read()).reason)
            }
            assertNull(recorder.connections.poll(300, TimeUnit.MILLISECONDS))
        }
    }

    @Test
    fun aNewConnectionTakesOverFromTheOldOne() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { first ->
                first.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, first.read())
                assertEquals(true, recorder.connections.poll(5, TimeUnit.SECONDS))
                Client(listener.port).use { second ->
                    second.send(hello())
                    assertInstanceOf(Messages.CompanionWelcome::class.java, second.read())
                    assertNull(first.read())
                    assertEquals(true, recorder.connections.poll(5, TimeUnit.SECONDS))
                    second.send(cursor(5, 6, Messages.CursorState.VISIBLE))
                    assertEquals(CursorSample(5, 6, visible = true, pressed = false),
                        recorder.samples.poll(5, TimeUnit.SECONDS))
                }
            }
            assertEquals(false, recorder.connections.poll(5, TimeUnit.SECONDS))
            assertNull(recorder.endings.poll(300, TimeUnit.MILLISECONDS))
        }
    }

    @Test
    fun stoppingSaysGoodbyeAndEnds() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                val stopping = thread { server.stop() }
                val goodbye = assertInstanceOf(Messages.ServerGoodbye::class.java, client.read())
                assertEquals(ErrorCode.NONE, goodbye.reason)
                client.close()
                stopping.join(5_000)
            }
            assertEquals(CompanionServer.Ending.STOPPED, recorder.endings.poll(5, TimeUnit.SECONDS))
            assertTrue(server.ended)
        }
    }

    @Test
    fun withoutAConnectionTheServerEndsWhenIdle() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing.copy(idleMillis = 200)).use { server ->
            server.setToken(token)
            server.start()
            assertEquals(CompanionServer.Ending.IDLE, recorder.endings.poll(5, TimeUnit.SECONDS))
            assertTrue(server.ended)
            Client(listener.port).use { client ->
                runCatching { client.send(hello()) }
                assertNull(client.read())
            }
        }
    }

    @Test
    fun aSilentClientIsDisconnected() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing.copy(silenceMillis = 300)).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                assertNull(client.read())
            }
            assertEquals(true, recorder.connections.poll(5, TimeUnit.SECONDS))
            assertEquals(false, recorder.connections.poll(5, TimeUnit.SECONDS))
        }
    }

    @Test
    fun typingEditorActionsAndKeyHintsReachThePhone() {
        val recorder = Recorder()
        val listener = SocketListener()
        val ime = FakeIme()
        val hints = FakeHints()
        CompanionServer(listener, recorder, recorder, timing, hints, ime).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                client.send(Messages.CommitText().also { it.text = "你好，world" })
                assertEquals("你好，world", ime.commits.poll(5, TimeUnit.SECONDS))
                client.send(Messages.EditorAction().also { it.action = ImeAction.SEND })
                assertEquals(ImeAction.SEND.wireValue(), ime.actions.poll(5, TimeUnit.SECONDS))
                client.send(Messages.EditorAction().also { it.action = ImeAction.UNSPECIFIED })
                assertEquals(ImeAction.UNSPECIFIED.wireValue(), ime.actions.poll(5, TimeUnit.SECONDS))
                val layout = Messages.KeyHintLayout()
                layout.hints.add(keyHint(KeyHintShape.RING, 16000, 50000, 9000, 20000, ""))
                layout.hints.add(keyHint(KeyHintShape.TAP, 60000, 30000, 0, 0, "F"))
                client.send(layout)
                assertEquals(
                    listOf(
                        HintItem(KeyHintShape.RING, 16000, 50000, 9000, 20000, ""),
                        HintItem(KeyHintShape.TAP, 60000, 30000, 0, 0, "F"),
                    ),
                    hints.layouts.poll(5, TimeUnit.SECONDS),
                )
                client.send(Messages.KeyHintState().also {
                    it.flags = Messages.KeyHintState.SHOWN or Messages.KeyHintState.FADED
                    it.opacity = 35
                })
                assertEquals(HintState(shown = true, faded = true, opacity = 35), hints.states.poll(5, TimeUnit.SECONDS))
                client.send(Messages.Goodbye())
                assertNull(client.read())
            }
            assertEquals(HintState.HIDDEN, hints.states.poll(5, TimeUnit.SECONDS))
            assertEquals(CursorSample.HIDDEN, recorder.samples.poll(5, TimeUnit.SECONDS))
        }
        assertTrue(ime.watchers.isEmpty())
    }

    @Test
    fun theInputBoxIsSentOnConnectAndWhenItChanges() {
        val recorder = Recorder()
        val listener = SocketListener()
        val ime = FakeIme()
        ime.box = InputBox(shown = true, inputType = 0x00004001, action = ImeAction.SEND.wireValue(), multiLine = false)
        CompanionServer(listener, recorder, recorder, timing, ime = ime).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                val open = assertInstanceOf(Messages.InputBoxChanged::class.java, client.read())
                assertTrue(open.shown())
                assertFalse(open.multiLine())
                assertEquals(0x00004001L, open.inputType)
                assertEquals(ImeAction.SEND, open.action)
                ime.change(InputBox(shown = true, inputType = 0x00020001, action = 0, multiLine = true))
                val multi = assertInstanceOf(Messages.InputBoxChanged::class.java, client.read())
                assertTrue(multi.shown())
                assertTrue(multi.multiLine())
                assertEquals(0x00020001L, multi.inputType)
                assertEquals(ImeAction.UNSPECIFIED, multi.action)
                ime.change(InputBox.HIDDEN)
                ime.change(InputBox.HIDDEN)
                val closed = assertInstanceOf(Messages.InputBoxChanged::class.java, client.read())
                assertFalse(closed.shown())
                client.send(Messages.Ping().also { it.nonce = 9 })
                assertEquals(9L, assertInstanceOf(Messages.Pong::class.java, client.read()).nonce)
            }
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                client.send(Messages.Ping().also { it.nonce = 10 })
                assertEquals(10L, assertInstanceOf(Messages.Pong::class.java, client.read()).nonce)
            }
        }
    }

    @Test
    fun aConnectionTheTransportAlreadyTrustsNeedsNoTokenAndAPersistentServerOutlivesGoodbye() {
        val recorder = Recorder()
        val listener = SocketListener(authenticated = true)
        CompanionServer(listener, recorder, recorder, timing.copy(idleMillis = 200), persistent = true).use { server ->
            server.start()
            repeat(2) {
                Client(listener.port).use { client ->
                    client.send(hello(presented = ByteArray(0)))
                    assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                    assertEquals(true, recorder.connections.poll(5, TimeUnit.SECONDS))
                    client.send(Messages.Goodbye())
                    assertNull(client.read())
                }
                assertEquals(false, recorder.connections.poll(5, TimeUnit.SECONDS))
            }
            assertNull(recorder.endings.poll(300, TimeUnit.MILLISECONDS))
            assertFalse(server.ended)
        }
    }

    @Test
    fun aScreenshotRequestIsAnsweredInChunksThatAddUpToThePicture() {
        val recorder = Recorder()
        val listener = SocketListener()
        val picture = ByteArray(40_000) { (it * 7 + 3).toByte() }
        val shots = FakeShots(ScreenshotResult.Picture(picture))
        CompanionServer(listener, recorder, recorder, timing, ime = FakeIme(), screenshots = shots).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                client.send(Messages.ScreenshotRequest().also {
                    it.requestId = 5
                    it.maxSide = 1280
                    it.quality = 80
                })
                assertEquals(1280 to 80, shots.requests.poll(5, TimeUnit.SECONDS))
                val chunks = List(3) { assertInstanceOf(Messages.ScreenshotChunk::class.java, client.read()) }
                assertEquals(listOf(0, 1, 2), chunks.map { it.index })
                assertTrue(chunks.all { it.total == 3 && it.requestId == 5L })
                assertEquals(listOf(15_360, 15_360, 9_280), chunks.map { it.data.size })
                assertArrayEquals(picture, chunks.fold(ByteArray(0)) { all, chunk -> all + chunk.data })
                client.send(Messages.Goodbye())
                assertNull(client.read())
            }
            assertEquals(true, shots.stops.poll(5, TimeUnit.SECONDS))
        }
    }

    @Test
    fun withoutScreenCaptureARequestFailsAsUnsupported() {
        val recorder = Recorder()
        val listener = SocketListener()
        CompanionServer(listener, recorder, recorder, timing, ime = FakeIme()).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                client.send(Messages.ScreenshotRequest().also { it.requestId = 1 })
                val failed = assertInstanceOf(Messages.ScreenshotFailed::class.java, client.read())
                assertEquals(1L, failed.requestId)
                assertEquals(ScreenshotProblem.UNSUPPORTED, failed.reason)
            }
        }
    }

    @Test
    fun aSecondRequestWhileTheFirstIsTakenIsBusyAndARefusalIsPassedOn() {
        val recorder = Recorder()
        val listener = SocketListener()
        val shots = FakeShots(null)
        CompanionServer(listener, recorder, recorder, timing, ime = FakeIme(), screenshots = shots).use { server ->
            server.setToken(token)
            server.start()
            Client(listener.port).use { client ->
                client.send(hello())
                assertInstanceOf(Messages.CompanionWelcome::class.java, client.read())
                client.send(Messages.ScreenshotRequest().also { it.requestId = 1 })
                val first = shots.pending.poll(5, TimeUnit.SECONDS)
                client.send(Messages.ScreenshotRequest().also { it.requestId = 2 })
                val busy = assertInstanceOf(Messages.ScreenshotFailed::class.java, client.read())
                assertEquals(2L, busy.requestId)
                assertEquals(ScreenshotProblem.BUSY, busy.reason)
                first!!(ScreenshotResult.Failure(ScreenshotProblem.REFUSED, "the screen capture was not allowed on the phone"))
                val refused = assertInstanceOf(Messages.ScreenshotFailed::class.java, client.read())
                assertEquals(1L, refused.requestId)
                assertEquals(ScreenshotProblem.REFUSED, refused.reason)
                assertEquals("the screen capture was not allowed on the phone", refused.message)
                assertEquals(1, shots.requests.size)
            }
            assertEquals(true, shots.stops.poll(5, TimeUnit.SECONDS))
        }
    }

    private class FakeShots(private val answer: ScreenshotResult?) : ScreenshotPort {
        val requests = LinkedBlockingQueue<Pair<Int, Int>>()
        val pending = LinkedBlockingQueue<(ScreenshotResult) -> Unit>()
        val stops = LinkedBlockingQueue<Boolean>()

        override fun capture(maxSide: Int, quality: Int, done: (ScreenshotResult) -> Unit) {
            requests.put(maxSide to quality)
            if (answer != null) {
                done(answer)
            } else {
                pending.put(done)
            }
        }

        override fun stop() {
            stops.put(true)
        }
    }

    private fun keyHint(shape: KeyHintShape, x: Int, y: Int, width: Int, height: Int, label: String) =
        Messages.KeyHint().also {
            it.shape = shape
            it.x = x
            it.y = y
            it.width = width
            it.height = height
            it.label = label
        }

    private fun hello(version: Int = Constants.COMPANION_VERSION, presented: ByteArray = token) =
        Messages.CompanionHello().also {
            it.version = version
            it.token = presented
        }

    private fun cursor(x: Int, y: Int, flags: Int) = Messages.CursorState().also {
        it.x = x
        it.y = y
        it.flags = flags
    }

    private class Recorder : CursorSink, CompanionServer.Events {
        val samples = LinkedBlockingQueue<CursorSample>()
        val connections = LinkedBlockingQueue<Boolean>()
        val endings = LinkedBlockingQueue<CompanionServer.Ending>()

        override fun update(sample: CursorSample) {
            samples.put(sample)
        }

        override fun connectionChanged(connected: Boolean) {
            connections.put(connected)
        }

        override fun ended(ending: CompanionServer.Ending) {
            endings.put(ending)
        }
    }

    private class FakeIme : ImePort {
        val commits = LinkedBlockingQueue<String>()
        val actions = LinkedBlockingQueue<Int>()
        val watchers = CopyOnWriteArrayList<(InputBox) -> Unit>()

        @Volatile
        override var box: InputBox = InputBox.HIDDEN

        override fun commit(text: String) {
            commits.put(text)
        }

        override fun perform(action: Int) {
            actions.put(action)
        }

        override fun watch(listener: (InputBox) -> Unit) {
            watchers.addIfAbsent(listener)
        }

        override fun unwatch(listener: (InputBox) -> Unit) {
            watchers.remove(listener)
        }

        fun change(next: InputBox) {
            box = next
            watchers.forEach { it(next) }
        }
    }

    private class FakeHints : KeyHintSink {
        val layouts = LinkedBlockingQueue<List<HintItem>>()
        val states = LinkedBlockingQueue<HintState>()

        override fun layout(items: List<HintItem>) {
            layouts.put(items)
        }

        override fun state(state: HintState) {
            states.put(state)
        }
    }

    private class SocketListener(private val authenticated: Boolean = false) : Listener {
        private val socket = ServerSocket(0, 8, InetAddress.getLoopbackAddress())
        val port: Int = socket.localPort

        override fun accept(): Connection = SocketConnection(socket.accept(), authenticated)

        override fun close() {
            socket.close()
        }
    }

    private class SocketConnection(private val socket: Socket, override val authenticated: Boolean = false) : Connection {
        override val input: InputStream = socket.getInputStream()
        override val output: OutputStream = socket.getOutputStream()

        override fun setReadTimeout(millis: Int) {
            socket.soTimeout = millis
        }

        override fun close() {
            socket.close()
        }
    }

    private class Client(port: Int) : AutoCloseable {
        private val socket = Socket(InetAddress.getLoopbackAddress(), port).also { it.soTimeout = 5_000 }
        private val input = DataInputStream(socket.getInputStream())

        fun send(message: Message) {
            sendRaw(Codec.encodeFrame(message))
        }

        fun sendRaw(frame: ByteArray) {
            socket.getOutputStream().write(frame)
            socket.getOutputStream().flush()
        }

        fun read(): Message? {
            val header = ByteArray(Constants.FRAME_HEADER_SIZE)
            return try {
                input.readFully(header)
                val frame = Codec.decodeFrameHeader(FrameContext.COMPANION, Direction.TO_CLIENT, header, 0, header.size)
                val payload = ByteArray(frame.length)
                input.readFully(payload)
                Codec.decodePayload(frame.type.create(), payload, 0, frame.length)
            } catch (e: SocketTimeoutException) {
                throw AssertionError("the companion neither answered nor closed the connection", e)
            } catch (e: IOException) {
                null
            }
        }

        override fun close() {
            socket.close()
        }
    }
}
