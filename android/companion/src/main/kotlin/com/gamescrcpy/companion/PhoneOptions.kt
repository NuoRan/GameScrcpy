package com.gamescrcpy.companion

import android.content.Context
import java.util.concurrent.CopyOnWriteArrayList

enum class CursorSize(val key: String, val scale: Float) {
    SMALL("small", 1f),
    MEDIUM("medium", 1.25f),
    LARGE("large", 1.6f),
}

enum class CursorStyle(val key: String) {
    ARROW("arrow"),
    DOT("dot"),
}

enum class ImeBar(val key: String) {
    BOTTOM("bottom"),
    TOP("top"),
}

class PhoneOptions(private val store: SyncStore) {
    fun interface Listener {
        fun changed()
    }

    private val listeners = CopyOnWriteArrayList<Listener>()

    @Volatile
    var cursorSize: CursorSize = CursorSize.entries.firstOrNull { it.key == store.read(CURSOR_SIZE) } ?: CursorSize.MEDIUM
        private set

    @Volatile
    var cursorStyle: CursorStyle =
        CursorStyle.entries.firstOrNull { it.key == store.read(CURSOR_STYLE) } ?: CursorStyle.ARROW
        private set

    @Volatile
    var imeBar: ImeBar = ImeBar.entries.firstOrNull { it.key == store.read(IME_BAR) } ?: ImeBar.BOTTOM
        private set

    fun addListener(listener: Listener) {
        listeners.addIfAbsent(listener)
    }

    fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }

    fun chooseCursorSize(size: CursorSize) {
        if (size == cursorSize) {
            return
        }
        cursorSize = size
        store.write(CURSOR_SIZE, size.key)
        changed()
    }

    fun chooseCursorStyle(style: CursorStyle) {
        if (style == cursorStyle) {
            return
        }
        cursorStyle = style
        store.write(CURSOR_STYLE, style.key)
        changed()
    }

    fun chooseImeBar(bar: ImeBar) {
        if (bar == imeBar) {
            return
        }
        imeBar = bar
        store.write(IME_BAR, bar.key)
        changed()
    }

    private fun changed() {
        listeners.forEach { it.changed() }
    }

    companion object {
        private const val FILE = "options"
        private const val CURSOR_SIZE = "cursorSize"
        private const val CURSOR_STYLE = "cursorStyle"
        private const val IME_BAR = "imeBar"

        @Volatile
        private var shared: PhoneOptions? = null

        fun of(context: Context): PhoneOptions = shared ?: synchronized(this) {
            shared ?: PhoneOptions(PreferenceSyncStore(context, FILE)).also { shared = it }
        }
    }
}
