package com.gamescrcpy.companion

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

    val connected: Boolean
        get() = localConnected || lanConnected

    fun update(listening: Boolean, connected: Boolean) {
        if (listening == this.listening && connected == localConnected) {
            return
        }
        this.listening = listening
        localConnected = connected
        listeners.forEach { it.changed() }
    }

    fun updateLan(state: LanState, connected: Boolean) {
        if (state == lan && connected == lanConnected) {
            return
        }
        lan = state
        lanConnected = connected
        listeners.forEach { it.changed() }
    }

    fun addListener(listener: Listener) {
        listeners.addIfAbsent(listener)
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }
}
