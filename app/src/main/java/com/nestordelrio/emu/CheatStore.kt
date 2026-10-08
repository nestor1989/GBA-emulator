package com.nestordelrio.emu

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** One cheat as the player typed it. [code] may have several lines. */
data class Cheat(val name: String, val code: String, val enabled: Boolean)

/** Cheats per game, stored as JSON in files/cheats/<CRC32>.json (included in Auto Backup). */
class CheatStore(context: Context, gameId: String) {

    private val file = File(File(context.filesDir, "cheats"), "$gameId.json")

    fun load(): MutableList<Cheat> {
        if (!file.isFile) return mutableListOf()
        return try {
            val array = JSONArray(file.readText())
            MutableList(array.length()) { i ->
                val item = array.getJSONObject(i)
                Cheat(
                    name = item.optString("name"),
                    code = item.optString("code"),
                    enabled = item.optBoolean("enabled", true),
                )
            }
        } catch (e: Exception) {
            mutableListOf()
        }
    }

    fun save(cheats: List<Cheat>) {
        val array = JSONArray()
        for (cheat in cheats) {
            array.put(
                JSONObject()
                    .put("name", cheat.name)
                    .put("code", cheat.code)
                    .put("enabled", cheat.enabled),
            )
        }
        file.parentFile?.mkdirs()
        val partial = File(file.path + ".tmp")
        partial.writeText(array.toString())
        if (!partial.renameTo(file)) {
            file.writeText(array.toString())
            partial.delete()
        }
    }
}
