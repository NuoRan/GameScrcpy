package com.gamescrcpy.companion

import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.Socket
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class LanConnection(private val socket: Socket, session: ByteArray) : Connection {
    private val closed = CountDownLatch(1)

    override val authenticated: Boolean
        get() = true

    override val input: InputStream =
        RecordInput(DataInputStream(socket.getInputStream()), RecordOpener(session, LanCodec.TO_COMPANION))

    override val output: OutputStream =
        RecordOutput(socket.getOutputStream(), RecordSealer(session, LanCodec.TO_COMPUTER))

    @Throws(IOException::class)
    override fun setReadTimeout(millis: Int) {
        socket.soTimeout = millis
    }

    override fun close() {
        runCatching { socket.close() }
        closed.countDown()
    }

    fun awaitClosed(millis: Long): Boolean = try {
        closed.await(millis, TimeUnit.MILLISECONDS)
    } catch (e: InterruptedException) {
        Thread.currentThread().interrupt()
        true
    }
}

private class RecordInput(private val source: DataInputStream, private val opener: RecordOpener) : InputStream() {
    private var frame = ByteArray(0)
    private var position = 0

    override fun read(): Int {
        if (!fill()) {
            return -1
        }
        return frame[position++].toInt() and 0xFF
    }

    override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
        if (length == 0) {
            return 0
        }
        if (!fill()) {
            return -1
        }
        val count = minOf(length, frame.size - position)
        System.arraycopy(frame, position, buffer, offset, count)
        position += count
        return count
    }

    override fun close() {
        source.close()
    }

    private fun fill(): Boolean {
        while (position >= frame.size) {
            val prefix = ByteArray(4)
            try {
                source.readFully(prefix)
            } catch (e: EOFException) {
                return false
            }
            val body = ByteArray(LanCodec.recordLength(prefix))
            source.readFully(body)
            frame = opener.open(prefix, body)
            position = 0
        }
        return true
    }
}

private class RecordOutput(private val target: OutputStream, private val sealer: RecordSealer) : OutputStream() {
    private val pending = ByteArrayOutputStream()

    override fun write(value: Int) {
        pending.write(value)
    }

    override fun write(buffer: ByteArray, offset: Int, length: Int) {
        pending.write(buffer, offset, length)
    }

    @Synchronized
    override fun flush() {
        if (pending.size() == 0) {
            target.flush()
            return
        }
        val frame = pending.toByteArray()
        pending.reset()
        target.write(sealer.seal(frame))
        target.flush()
    }

    override fun close() {
        target.close()
    }
}

class QueueListener : Listener {
    private val queue = LinkedBlockingQueue<Any>()

    @Volatile
    private var closed = false

    fun offer(connection: Connection) {
        if (closed) {
            connection.close()
            return
        }
        queue.offer(connection)
    }

    @Throws(IOException::class)
    override fun accept(): Connection {
        val next = try {
            queue.take()
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IOException("interrupted", e)
        }
        if (next === CLOSED) {
            queue.offer(CLOSED)
            throw IOException("the listener is closed")
        }
        return next as Connection
    }

    override fun close() {
        closed = true
        while (true) {
            val waiting = queue.poll() ?: break
            if (waiting is Connection) {
                waiting.close()
            }
        }
        queue.offer(CLOSED)
    }

    private companion object {
        val CLOSED = Any()
    }
}
