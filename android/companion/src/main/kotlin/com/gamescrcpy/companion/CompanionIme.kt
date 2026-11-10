package com.gamescrcpy.companion

import android.inputmethodservice.InputMethodService
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.TextView

class CompanionIme : InputMethodService(), ImeTarget {
    private val main = Handler(Looper.getMainLooper())
    private val fallback = ImeFallback(FALLBACK_MILLIS)
    private val check = Runnable { checkConnection() }
    private val stateListener = CompanionState.Listener { main.post { connectionChanged() } }
    private var strip: TextView? = null

    override fun onCreate() {
        super.onCreate()
        ImeBridge.attach(this)
        CompanionState.addListener(stateListener)
        connectionChanged()
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        CompanionState.removeListener(stateListener)
        ImeBridge.detach(this)
        strip = null
        super.onDestroy()
    }

    override fun onCreateInputView(): View {
        val padding = dp(STRIP_PADDING_DP)
        val view = TextView(this).apply {
            gravity = Gravity.CENTER
            setTextColor(KeyHintLook.TEXT)
            setBackgroundColor(STRIP_COLOR)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, STRIP_TEXT_SP)
            minHeight = dp(STRIP_HEIGHT_DP)
            setPadding(padding, padding / 2, padding, padding / 2)
            setOnClickListener { switchBack() }
        }
        strip = view
        refreshStrip()
        return view
    }

    override fun onEvaluateFullscreenMode(): Boolean = false

    override fun onEvaluateInputViewShown(): Boolean {
        super.onEvaluateInputViewShown()
        return true
    }

    override fun onShowInputRequested(flags: Int, configChange: Boolean): Boolean = true

    override fun onStartInputView(info: EditorInfo, restarting: Boolean) {
        super.onStartInputView(info, restarting)
        ImeBridge.report(InputBoxes.of(info.inputType, info.imeOptions))
        refreshStrip()
    }

    override fun onFinishInputView(finishingInput: Boolean) {
        ImeBridge.report(InputBox.HIDDEN)
        super.onFinishInputView(finishingInput)
    }

    override fun onFinishInput() {
        ImeBridge.report(InputBox.HIDDEN)
        super.onFinishInput()
    }

    override fun commit(text: String) {
        main.post { currentInputConnection?.commitText(text, 1) }
    }

    override fun perform(action: Int) {
        main.post { performNow(action) }
    }

    private fun performNow(action: Int) {
        val connection = currentInputConnection ?: return
        when (action) {
            EditorInfo.IME_ACTION_UNSPECIFIED -> sendKeyChar('\n')
            EditorInfo.IME_ACTION_NONE -> sendDownUpKeyEvents(KeyEvent.KEYCODE_ENTER)
            else -> connection.performEditorAction(action)
        }
    }

    private fun connectionChanged() {
        main.removeCallbacks(check)
        refreshStrip()
        val delay = fallback.update(CompanionState.connected, SystemClock.uptimeMillis()) ?: return
        main.postDelayed(check, delay + 1) // source-rule-allow R9: going back to the phone's keyboard a few seconds after the computer is gone is the behaviour
    }

    private fun checkConnection() {
        if (fallback.due(CompanionState.connected, SystemClock.uptimeMillis())) {
            switchBack()
        } else {
            connectionChanged()
        }
    }

    private fun switchBack() {
        if (!switchToPreviousInputMethod()) {
            switchToNextInputMethod(false)
        }
    }

    private fun refreshStrip() {
        val view = strip ?: return
        view.setText(if (CompanionState.connected) R.string.ime_typing else R.string.ime_waiting)
    }

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics).toInt()

    private companion object {
        const val FALLBACK_MILLIS = 5_000L
        const val STRIP_HEIGHT_DP = 32f
        const val STRIP_PADDING_DP = 12f
        const val STRIP_TEXT_SP = 13f
        const val STRIP_COLOR = 0xF01B1C1F.toInt()
    }
}
