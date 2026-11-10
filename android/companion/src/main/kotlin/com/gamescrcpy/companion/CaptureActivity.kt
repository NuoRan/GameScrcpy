package com.gamescrcpy.companion

import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle

class CaptureGrant(val resultCode: Int, val data: Intent)

object CaptureGrants {
    @Volatile
    private var listener: ((CaptureGrant?) -> Unit)? = null

    fun listen(callback: ((CaptureGrant?) -> Unit)?) {
        listener = callback
    }

    fun deliver(grant: CaptureGrant?) {
        listener?.invoke(grant)
    }
}

class CaptureActivity : Activity() {
    private var answered = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState != null) {
            return
        }
        val manager = getSystemService(MediaProjectionManager::class.java)
        if (manager == null) {
            answer(null)
            return
        }
        try {
            startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_CAPTURE)
        } catch (e: RuntimeException) {
            answer(null)
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_CAPTURE) {
            return
        }
        answer(if (resultCode == RESULT_OK && data != null) CaptureGrant(resultCode, data) else null)
    }

    override fun onDestroy() {
        if (isFinishing) {
            answer(null)
        }
        super.onDestroy()
    }

    private fun answer(grant: CaptureGrant?) {
        if (answered) {
            return
        }
        answered = true
        CaptureGrants.deliver(grant)
        finish()
    }

    private companion object {
        const val REQUEST_CAPTURE = 1
    }
}
