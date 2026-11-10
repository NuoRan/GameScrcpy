package com.gamescrcpy.companion

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

class LocalListener(name: String) : Listener {
    private val socket = LocalServerSocket(name)

    @Throws(IOException::class)
    override fun accept(): Connection = LocalConnection(socket.accept())

    override fun close() {
        quietly { Os.shutdown(socket.fileDescriptor, OsConstants.SHUT_RDWR) }
        quietly { socket.close() }
    }
}

private class LocalConnection(private val socket: LocalSocket) : Connection {
    override val input: InputStream = socket.inputStream
    override val output: OutputStream = socket.outputStream

    @Throws(IOException::class)
    override fun setReadTimeout(millis: Int) {
        socket.soTimeout = millis
    }

    override fun close() {
        quietly { socket.shutdownInput() }
        quietly { socket.shutdownOutput() }
        quietly { socket.close() }
    }
}

private inline fun quietly(action: () -> Unit) {
    try {
        action()
    } catch (e: IOException) {
        return
    } catch (e: ErrnoException) {
        return
    }
}
