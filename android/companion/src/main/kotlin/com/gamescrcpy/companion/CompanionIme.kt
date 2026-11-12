package com.gamescrcpy.companion

import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.LayerDrawable
import android.inputmethodservice.InputMethodService
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Space
import android.widget.TextView

class CompanionIme : InputMethodService(), ImeTarget {
    private val main = Handler(Looper.getMainLooper())
    private val fallback = ImeFallback(FALLBACK_MILLIS)
    private val check = Runnable { checkConnection() }
    private val stateListener = CompanionState.Listener { main.post { connectionChanged() } }
    private val hostWatcher: () -> Unit = { main.post { placeHint() } }
    private val optionsListener = PhoneOptions.Listener { main.post { placeHint() } }
    private var strip: TextView? = null
    private var onTop = false

    override fun onCreate() {
        super.onCreate()
        ImeBridge.attach(this)
        CompanionState.addListener(stateListener)
        ImeBridge.watchHost(hostWatcher)
        PhoneOptions.of(this).addListener(optionsListener)
        connectionChanged()
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        CompanionState.removeListener(stateListener)
        ImeBridge.unwatchHost(hostWatcher)
        PhoneOptions.of(this).removeListener(optionsListener)
        ImeBridge.detach(this)
        strip = null
        super.onDestroy()
    }

    override fun onCreateInputView(): View {
        onTop = wantsTop()
        if (onTop) {
            strip = null
            return Space(this)
        }
        val ui = Ui(this, Look.palette(this, null))
        val face = ColorDrawable(ui.palette.bgElevated)
        val edge = ColorDrawable(ui.palette.strokeSubtle)
        val view = ui.text(Look.LABEL, ui.palette.textPrimary).apply {
            gravity = Gravity.CENTER
            minHeight = ui.px(STRIP_HEIGHT_DP)
            setPaddingRelative(ui.px(Look.SPACE_MD), ui.px(Look.SPACE_XS), ui.px(Look.SPACE_MD), ui.px(Look.SPACE_XS))
            background = LayerDrawable(arrayOf<Drawable>(face, edge)).apply {
                setLayerGravity(1, Gravity.TOP or Gravity.FILL_HORIZONTAL)
                setLayerHeight(1, ui.px(Look.STROKE))
            }
            ui.lead(this, R.drawable.ic_keyboard, ui.palette.accent)
            setOnClickListener { switchToPhoneKeyboard() }
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

    override fun switchBack() {
        main.post { switchToPhoneKeyboard() }
    }

    private fun wantsTop(): Boolean = PhoneOptions.of(this).imeBar == ImeBar.TOP && ImeBridge.topHost

    private fun placeHint() {
        if (wantsTop() != onTop) {
            setInputView(onCreateInputView())
        }
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
            switchToPhoneKeyboard()
        } else {
            connectionChanged()
        }
    }

    private fun switchToPhoneKeyboard() {
        if (!switchToPreviousInputMethod()) {
            switchToNextInputMethod(false)
        }
    }

    private fun refreshStrip() {
        val view = strip ?: return
        view.setText(if (CompanionState.connected) R.string.ime_typing else R.string.ime_waiting)
    }

    private companion object {
        const val FALLBACK_MILLIS = 5_000L
        const val STRIP_HEIGHT_DP = 32f
    }
}
