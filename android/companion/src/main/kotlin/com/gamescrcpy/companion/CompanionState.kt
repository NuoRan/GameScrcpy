package com.gamescrcpy.companion

import android.os.SystemClock
import java.util.concurrent.CopyOnWriteArrayList

object CompanionState {
    fun interface Listener {
        fun changed()
    }

    private val listeners = CopyOnWriteArrayList<Listener>()

    @Volatile
    var listening = false
        private set

    @Volatile
    var localConnected = false
        private set

    @Volatile
    var lanConnected = false
        private set

    @Volatile
    var lan = LanState.IDLE
        private set

    @Volatile
    var connectedAt = 0L
        private set

    @Volatile
    var connectedClock = 0L
        private set

    val connected: Boolean
        get() = localConnected || lanConnected

    fun update(listening: Boolean, connected: Boolean) {
        if (listening == this.listening && connected == localConnected) {
            return
        }
        val joined = connected && !this.connected
        this.listening = listening
        localConnected = connected
        if (joined) {
            stamp()
        }
        listeners.forEach { it.changed() }
    }

    fun updateLan(state: LanState, connected: Boolean) {
        if (state == lan && connected == lanConnected) {
            return
        }
        val joined = connected && !this.connected
        lan = state
        lanConnected = connected
        if (joined) {
            stamp()
        }
        listeners.forEach { it.changed() }
    }

    fun addListener(listener: Listener) {
        listeners.addIfAbsent(listener)
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }

    private fun stamp() {
        connectedAt = System.currentTimeMillis()
        connectedClock = SystemClock.elapsedRealtime()
    }
}
