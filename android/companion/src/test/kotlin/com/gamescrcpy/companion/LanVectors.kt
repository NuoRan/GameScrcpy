package com.gamescrcpy.companion

import java.io.File
import org.json.JSONObject

object LanVectors {
    fun load(): JSONObject {
        val directory = File(System.getProperty("gs.protocol.vectorDir") ?: "../../protocol/vectors")
        val text = File(File(directory, "companion"), "lan.json").readText(Charsets.UTF_8)
        return JSONObject(text).getJSONArray("vectors").getJSONObject(0)
    }

    fun bytes(hex: String): ByteArray = Hex.decode(hex) ?: throw AssertionError("not hex: $hex")
}
