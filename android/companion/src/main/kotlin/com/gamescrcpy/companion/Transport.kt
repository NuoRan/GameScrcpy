package com.gamescrcpy.companion

import java.io.Closeable
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

interface Connection : Closeable {
    val input: InputStream
    val output: OutputStream
    val authenticated: Boolean
        get() = false

    @Throws(IOException::class)
    fun setReadTimeout(millis: Int)

    override fun close()
}

interface Listener : Closeable {
    @Throws(IOException::class)
    fun accept(): Connection

    override fun close()
}
