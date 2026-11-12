package com.gamescrcpy.companion

import android.text.InputType
import android.view.inputmethod.EditorInfo
import java.util.concurrent.CopyOnWriteArrayList

data class InputBox(val shown: Boolean, val inputType: Long, val action: Int, val multiLine: Boolean) {
    companion object {
        val HIDDEN = InputBox(shown = false, inputType = 0, action = EditorInfo.IME_ACTION_UNSPECIFIED, multiLine = false)
    }
}

object InputBoxes {
    private const val LAST_ACTION = EditorInfo.IME_ACTION_PREVIOUS

    fun of(inputType: Int, imeOptions: Int): InputBox {
        if (inputType == InputType.TYPE_NULL) {
            return InputBox.HIDDEN
        }
        val masked = imeOptions and EditorInfo.IME_MASK_ACTION
        val action = if (masked in EditorInfo.IME_ACTION_UNSPECIFIED..LAST_ACTION) masked else EditorInfo.IME_ACTION_UNSPECIFIED
        val text = (inputType and InputType.TYPE_MASK_CLASS) == InputType.TYPE_CLASS_TEXT
        val multiLine = text && (inputType and InputType.TYPE_TEXT_FLAG_MULTI_LINE) != 0
        return InputBox(shown = true, inputType = inputType.toLong() and 0xFFFFFFFFL, action = action, multiLine = multiLine)
    }
}

class ImeFallback(private val graceMillis: Long) {
    private var lostAt: Long? = null

    fun update(connected: Boolean, now: Long): Long? {
        if (connected) {
            lostAt = null
            return null
        }
        val since = lostAt ?: now.also { lostAt = it }
        return (since + graceMillis - now).coerceAtLeast(0)
    }

    fun due(connected: Boolean, now: Long): Boolean {
        if (connected) {
            lostAt = null
            return false
        }
        val since = lostAt ?: return false
        return now - since >= graceMillis
    }
}

interface ImeTarget {
    fun commit(text: String)

    fun perform(action: Int)

    fun switchBack() = Unit
}

interface ImePort {
    val box: InputBox

    fun commit(text: String)

    fun perform(action: Int)

    fun watch(listener: (InputBox) -> Unit)

    fun unwatch(listener: (InputBox) -> Unit)
}

object ImeBridge : ImePort {
    private val listeners = CopyOnWriteArrayList<(InputBox) -> Unit>()
    private val hostListeners = CopyOnWriteArrayList<() -> Unit>()

    @Volatile
    private var target: ImeTarget? = null

    @Volatile
    override var box: InputBox = InputBox.HIDDEN
        private set

    @Volatile
    var topHost: Boolean = false
        private set

    fun hostTop(on: Boolean) {
        if (topHost == on) {
            return
        }
        topHost = on
        hostListeners.forEach { it() }
    }

    fun watchHost(listener: () -> Unit) {
        hostListeners.addIfAbsent(listener)
    }

    fun unwatchHost(listener: () -> Unit) {
        hostListeners.remove(listener)
    }

    fun switchBack() {
        target?.switchBack()
    }

    fun attach(ime: ImeTarget) {
        target = ime
    }

    fun detach(ime: ImeTarget) {
        if (target === ime) {
            target = null
            report(InputBox.HIDDEN)
        }
    }

    fun report(state: InputBox) {
        if (state == box) {
            return
        }
        box = state
        listeners.forEach { it(state) }
    }

    override fun commit(text: String) {
        target?.commit(text)
    }

    override fun perform(action: Int) {
        target?.perform(action)
    }

    override fun watch(listener: (InputBox) -> Unit) {
        listeners.addIfAbsent(listener)
    }

    override fun unwatch(listener: (InputBox) -> Unit) {
        listeners.remove(listener)
    }
}
