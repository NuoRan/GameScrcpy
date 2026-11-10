package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.DecodeError
import com.gamescrcpy.protocol.DecodeException
import com.gamescrcpy.protocol.Enums.ErrorCode
import com.gamescrcpy.protocol.Enums.ImeAction
import com.gamescrcpy.protocol.Enums.OverlayStatus
import com.gamescrcpy.protocol.Enums.ScreenshotProblem
import com.gamescrcpy.protocol.Message
import com.gamescrcpy.protocol.Messages
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

class CompanionServer(
    private val listener: Listener,
    private val sink: CursorSink,
    private val events: Events,
    private val timing: Timing = Timing(),
    private val hints: KeyHintSink = KeyHintSink.NONE,
    private val ime: ImePort = ImeBridge,
    private val persistent: Boolean = false,
    private val screenshots: ScreenshotPort = ScreenshotPort.NONE,
) : AutoCloseable {

    data class Timing(
        val idleMillis: Long = 15_000,
        val helloMillis: Int = 5_000,
        val silenceMillis: Int = 6_000,
        val drainMillis: Long = 5_000,
    )

    enum class Ending { IDLE, CLIENT_GOODBYE, STOPPED }

    interface Events {
        fun connectionChanged(connected: Boolean)

        fun ended(ending: Ending)
    }

    private enum class Admission { ADMITTED, REFUSED, CLOSED }

    private sealed interface Outgoing {
        class Send(val message: Message) : Outgoing

        data object End : Outgoing
    }

    private class Link(val connection: Connection) {
        val queue = LinkedBlockingQueue<Outgoing>()
        val ended = CountDownLatch(1)
        var lastOverlay: OverlayInfo? = null
        var lastBox: InputBox? = null
        var clientGoodbye = false
        var writing = false

        @Volatile
        var shooting = false

        @Volatile
        var shot = false

        fun enqueue(item: Outgoing) {
            queue.offer(item)
        }

        @Throws(IOException::class)
        fun write(message: Message) {
            val output = connection.output
            output.write(Codec.encodeFrame(message))
            output.flush()
        }

        fun abort() {
            queue.offer(Outgoing.End)
            connection.close()
        }
    }

    private val lock = ReentrantLock()
    private val changed = lock.newCondition()
    private var token: ByteArray? = null
    private var overlayInfo = OverlayInfo.NONE
    @Volatile
    private var active: Link? = null
    private val links = HashSet<Link>()
    private var idleSince = System.nanoTime()
    private var closed = false
    private var ending: Ending? = null
    private val boxWatcher: (InputBox) -> Unit = { box -> boxChanged(box) }

    val ended: Boolean
        get() = lock.withLock { closed || ending != null }

    fun start() {
        lock.withLock { idleSince = System.nanoTime() }
        ime.watch(boxWatcher)
        spawn("companion-accept") { acceptLoop() }
        if (!persistent) {
            spawn("companion-idle") { watchIdle() }
        }
    }

    fun setToken(value: ByteArray) {
        lock.withLock {
            token = value.copyOf()
            if (active == null) {
                idleSince = System.nanoTime()
                changed.signalAll()
            }
        }
    }

    fun overlayChanged(info: OverlayInfo) {
        lock.withLock {
            overlayInfo = info
            val link = active ?: return
            if (link.lastOverlay == info) {
                return
            }
            link.lastOverlay = info
            link.enqueue(Outgoing.Send(overlayChangedMessage(info)))
        }
    }

    fun stop() {
        var link: Link? = null
        lock.withLock {
            if (closed || ending != null) {
                return
            }
            ending = Ending.STOPPED
            link = active
            changed.signalAll()
        }
        link?.let { stopping ->
            stopping.enqueue(Outgoing.Send(serverGoodbye(ErrorCode.NONE, "")))
            stopping.enqueue(Outgoing.End)
            try {
                stopping.ended.await(timing.drainMillis, TimeUnit.MILLISECONDS)
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
            }
            stopping.abort()
        }
        events.ended(Ending.STOPPED)
    }

    override fun close() {
        val all: List<Link>
        lock.withLock {
            if (closed) {
                return
            }
            closed = true
            all = links.toList()
            changed.signalAll()
        }
        ime.unwatch(boxWatcher)
        listener.close()
        all.forEach { it.abort() }
        sink.update(CursorSample.HIDDEN)
        hints.state(HintState.HIDDEN)
    }

    private fun spawn(name: String, body: () -> Unit) {
        val thread = Thread(body, name)
        thread.isDaemon = true
        thread.start()
    }

    private fun acceptLoop() {
        while (true) {
            val connection = try {
                listener.accept()
            } catch (e: IOException) {
                return
            } catch (e: RuntimeException) {
                return
            }
            val link = Link(connection)
            val admitted: Boolean
            val stopped: Boolean
            lock.withLock {
                stopped = closed
                admitted = !closed && ending == null
                if (admitted) {
                    links.add(link)
                }
            }
            if (!admitted) {
                connection.close()
                if (stopped) {
                    return
                }
                continue
            }
            spawn("companion-link") { serve(link) }
        }
    }

    private fun watchIdle() {
        val idleNanos = TimeUnit.MILLISECONDS.toNanos(timing.idleMillis)
        var reached = false
        try {
            lock.withLock {
                while (!closed && ending == null) {
                    if (active != null) {
                        changed.await()
                        continue
                    }
                    val left = idleNanos - (System.nanoTime() - idleSince)
                    if (left <= 0) {
                        ending = Ending.IDLE
                        reached = true
                        changed.signalAll()
                        break
                    }
                    changed.awaitNanos(left)
                }
            }
        } catch (e: InterruptedException) {
            return
        }
        if (reached) {
            events.ended(Ending.IDLE)
        }
    }

    private fun serve(link: Link) {
        try {
            converse(link)
        } catch (e: IOException) {
            link.clientGoodbye = false
        } catch (e: RuntimeException) {
            link.clientGoodbye = false
        } finally {
            link.abort()
            release(link)
        }
    }

    private fun converse(link: Link) {
        val connection = link.connection
        val reader = FrameReader(connection.input)
        connection.setReadTimeout(timing.helloMillis)
        val first = try {
            reader.read()
        } catch (e: DecodeException) {
            refuse(link, refusalFor(e.error()), e.error().specName())
            return
        }
        if (first !is Messages.CompanionHello) {
            refuse(link, ErrorCode.UNEXPECTED_MESSAGE, "expected CompanionHello, got ${first.type().specName()}")
            return
        }
        if (first.version != Constants.COMPANION_VERSION) {
            refuse(
                link,
                ErrorCode.VERSION_MISMATCH,
                "companion protocol version ${Constants.COMPANION_VERSION}, client version ${first.version}",
            )
            return
        }
        when (admit(link, first.token)) {
            Admission.CLOSED -> return
            Admission.REFUSED -> {
                refuse(link, ErrorCode.AUTHENTICATION_FAILED, "wrong token")
                return
            }
            Admission.ADMITTED -> Unit
        }
        link.writing = true
        spawn("companion-writer") { writeLoop(link) }
        events.connectionChanged(true)
        connection.setReadTimeout(timing.silenceMillis)
        while (true) {
            val message = try {
                reader.read()
            } catch (e: DecodeException) {
                refuse(link, refusalFor(e.error()), e.error().specName())
                return
            }
            when (message) {
                is Messages.CursorState -> {
                    if (active === link) {
                        sink.update(CursorSample(message.x, message.y, message.visible(), message.pressed()))
                    }
                }
                is Messages.CommitText -> {
                    if (active === link) {
                        ime.commit(message.text)
                    }
                }
                is Messages.EditorAction -> {
                    if (active === link) {
                        ime.perform(message.action.wireValue())
                    }
                }
                is Messages.KeyHintLayout -> {
                    if (active === link) {
                        hints.layout(KeyHints.from(message))
                    }
                }
                is Messages.KeyHintState -> {
                    if (active === link) {
                        hints.state(KeyHints.from(message))
                    }
                }
                is Messages.ScreenshotRequest -> {
                    if (active === link) {
                        screenshot(link, message)
                    }
                }
                is Messages.Ping -> link.enqueue(Outgoing.Send(pong(message)))
                is Messages.Goodbye -> {
                    link.clientGoodbye = true
                    return
                }
                else -> {
                    refuse(link, ErrorCode.UNEXPECTED_MESSAGE, "unexpected ${message.type().specName()}")
                    return
                }
            }
        }
    }

    private fun admit(link: Link, presented: ByteArray): Admission {
        var previous: Link? = null
        lock.withLock {
            if (closed || ending != null) {
                return Admission.CLOSED
            }
            if (!link.connection.authenticated && !Tokens.matches(token, presented)) {
                return Admission.REFUSED
            }
            previous = active
            active = link
            link.lastOverlay = overlayInfo
            link.enqueue(Outgoing.Send(welcome(overlayInfo)))
            val box = ime.box
            link.lastBox = box
            if (box != InputBox.HIDDEN) {
                link.enqueue(Outgoing.Send(inputBoxMessage(box)))
            }
            changed.signalAll()
        }
        previous?.abort()
        return Admission.ADMITTED
    }

    private fun screenshot(link: Link, request: Messages.ScreenshotRequest) {
        val requestId = request.requestId
        if (link.shooting) {
            link.enqueue(
                Outgoing.Send(
                    ScreenshotChunks.failed(requestId, ScreenshotProblem.BUSY, "a screenshot is still being taken"),
                ),
            )
            return
        }
        link.shooting = true
        link.shot = true
        screenshots.capture(request.maxSide, request.quality) { result ->
            link.shooting = false
            val replies: List<Message> = when (result) {
                is ScreenshotResult.Picture ->
                    if (ScreenshotChunks.fits(result.jpeg)) {
                        ScreenshotChunks.split(requestId, result.jpeg)
                    } else {
                        listOf(
                            ScreenshotChunks.failed(requestId, ScreenshotProblem.ERROR, "the screenshot does not fit into 4 MiB"),
                        )
                    }
                is ScreenshotResult.Failure -> listOf(ScreenshotChunks.failed(requestId, result.problem, result.text))
            }
            replies.forEach { link.enqueue(Outgoing.Send(it)) }
        }
    }

    private fun boxChanged(box: InputBox) {
        lock.withLock {
            val link = active ?: return
            if (link.lastBox == box) {
                return
            }
            link.lastBox = box
            link.enqueue(Outgoing.Send(inputBoxMessage(box)))
        }
    }

    private fun refuse(link: Link, code: ErrorCode, text: String) {
        val goodbye = serverGoodbye(code, text)
        if (link.writing) {
            link.enqueue(Outgoing.Send(goodbye))
            link.enqueue(Outgoing.End)
        } else {
            link.write(goodbye)
        }
        drain(link)
    }

    private fun drain(link: Link) {
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timing.drainMillis)
        val input = link.connection.input
        val scratch = ByteArray(512)
        while (true) {
            val left = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime())
            if (left <= 0) {
                return
            }
            link.connection.setReadTimeout(left.coerceAtMost(Int.MAX_VALUE.toLong()).toInt())
            if (input.read(scratch) < 0) {
                return
            }
        }
    }

    private fun writeLoop(link: Link) {
        try {
            while (true) {
                when (val item = link.queue.take()) {
                    is Outgoing.Send -> link.write(item.message)
                    Outgoing.End -> return
                }
            }
        } catch (e: IOException) {
            link.abort()
        } catch (e: InterruptedException) {
            link.abort()
        } catch (e: RuntimeException) {
            link.abort()
        }
    }

    private fun release(link: Link) {
        var wasActive = false
        var goodbye = false
        lock.withLock {
            links.remove(link)
            if (active === link) {
                active = null
                wasActive = true
                idleSince = System.nanoTime()
                if (link.clientGoodbye && !closed && ending == null && !persistent) {
                    ending = Ending.CLIENT_GOODBYE
                    goodbye = true
                }
                changed.signalAll()
            }
        }
        if (wasActive) {
            sink.update(CursorSample.HIDDEN)
            hints.state(HintState.HIDDEN)
            events.connectionChanged(false)
        }
        if (link.shot) {
            screenshots.stop()
        }
        if (goodbye) {
            events.ended(Ending.CLIENT_GOODBYE)
        }
        link.ended.countDown()
    }

    private fun refusalFor(error: DecodeError): ErrorCode = when (error) {
        DecodeError.FRAME_TOO_LARGE -> ErrorCode.FRAME_TOO_LARGE
        DecodeError.UNKNOWN_TYPE -> ErrorCode.UNKNOWN_TYPE
        DecodeError.WRONG_DIRECTION -> ErrorCode.WRONG_DIRECTION
        DecodeError.UNEXPECTED_TYPE -> ErrorCode.UNEXPECTED_MESSAGE
        else -> ErrorCode.MALFORMED_FRAME
    }

    private fun welcome(info: OverlayInfo): Messages.CompanionWelcome {
        val message = Messages.CompanionWelcome()
        message.version = Constants.COMPANION_VERSION
        message.overlay = statusOf(info)
        message.width = dimension(info, info.width)
        message.height = dimension(info, info.height)
        message.rotation = info.rotation.coerceIn(0, 3)
        return message
    }

    private fun overlayChangedMessage(info: OverlayInfo): Messages.OverlayChanged {
        val message = Messages.OverlayChanged()
        message.overlay = statusOf(info)
        message.width = dimension(info, info.width)
        message.height = dimension(info, info.height)
        message.rotation = info.rotation.coerceIn(0, 3)
        return message
    }

    private fun inputBoxMessage(box: InputBox): Messages.InputBoxChanged {
        val message = Messages.InputBoxChanged()
        message.flags = (if (box.shown) Messages.InputBoxChanged.SHOWN else 0) or
            (if (box.multiLine) Messages.InputBoxChanged.MULTI_LINE else 0)
        message.inputType = box.inputType and 0xFFFFFFFFL
        message.action = ImeAction.CODEC.fromWire(box.action) ?: ImeAction.UNSPECIFIED
        return message
    }

    private fun statusOf(info: OverlayInfo): OverlayStatus =
        if (info.granted) OverlayStatus.GRANTED else OverlayStatus.NO_PERMISSION

    private fun dimension(info: OverlayInfo, value: Int): Int =
        if (info.granted) value.coerceIn(0, MAX_U16) else 0

    private fun pong(ping: Messages.Ping): Messages.Pong {
        val message = Messages.Pong()
        message.nonce = ping.nonce
        message.clientTimeUs = ping.clientTimeUs
        message.serverTimeUs = TimeUnit.NANOSECONDS.toMicros(System.nanoTime()).coerceAtLeast(0)
        return message
    }

    private fun serverGoodbye(code: ErrorCode, text: String): Messages.ServerGoodbye {
        val message = Messages.ServerGoodbye()
        message.reason = code
        message.message = if (text.length > MAX_TEXT) text.substring(0, MAX_TEXT) else text
        return message
    }

    private companion object {
        const val MAX_U16 = 0xFFFF
        const val MAX_TEXT = 256
    }
}
