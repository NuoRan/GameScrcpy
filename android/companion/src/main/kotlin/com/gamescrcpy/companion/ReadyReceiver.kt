package com.gamescrcpy.companion

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class ReadyReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (isOrderedBroadcast) {
            resultCode = if (CompanionState.listening) READY else NOT_READY
        }
    }

    private companion object {
        const val READY = 1
        const val NOT_READY = 0
    }
}
