package com.nestordelrio.emu

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.nestordelrio.emu.core.Emulator
import java.io.File
import java.io.IOException

/**
 * Where games and saves live.
 *
 * - The opened game is copied to noBackupFilesDir: archives need random access,
 *   which some providers (Drive, cloud folders) do not offer, and ROMs must
 *   never end up in Google's cloud backup.
 * - Saves live in filesDir, which Auto Backup includes. States live there too
 *   but are left out of the backup (res/xml/backup_rules.xml): they are big,
 *   and going over the 25 MB quota stops the whole backup, saves included.
 * - The player can also keep saves in a folder of their own: see [SaveFolder].
 */
class RomStore(private val context: Context) {

    data class Recent(val uri: Uri?, val name: String, val path: String)

    sealed interface ImportResult {
        data class Ok(val file: File, val name: String) : ImportResult
        data class Error(val messageRes: Int) : ImportResult
    }

    val savesDir = File(context.filesDir, "saves")
    val statesDir = File(context.filesDir, "states")
    private val romsDir = File(context.noBackupFilesDir, "roms")
    private val bundledDir = File(context.noBackupFilesDir, "games")
    private val prefs = context.getSharedPreferences("recent", Context.MODE_PRIVATE)

    fun recent(): Recent? {
        val path = prefs.getString(KEY_PATH, null) ?: return null
        val name = prefs.getString(KEY_NAME, null) ?: File(path).nameWithoutExtension
        val uri = prefs.getString(KEY_URI, null)?.let(Uri::parse)
        return Recent(uri, name, path)
    }

    fun saveRecent(uri: Uri?, name: String, path: String) {
        prefs.edit()
            .putString(KEY_URI, uri?.toString())
            .putString(KEY_NAME, name)
            .putString(KEY_PATH, path)
            .apply()
    }

    /** The game that ships with the app (assets/games: Cometa, our own homebrew), or null. */
    fun bundledGame(): String? =
        runCatching { context.assets.list("games") }.getOrNull()
            ?.sorted()
            ?.firstOrNull { it.substringAfterLast('.', "").lowercase() in GAME_EXTENSIONS }

    fun isBundled(path: String): Boolean = File(path).parentFile == bundledDir

    /**
     * Copies the bundled game out of the APK and returns its path. It is a few
     * KB, so it is copied on every launch: an app update always runs the new
     * version. Call off the main thread.
     */
    fun prepareBundled(asset: String): ImportResult {
        return try {
            bundledDir.mkdirs()
            val target = File(bundledDir, asset)
            val partial = File(bundledDir, "$asset.part")
            context.assets.open("games/$asset").use { input ->
                partial.outputStream().use { output -> input.copyTo(output) }
            }
            if (!partial.renameTo(target)) throw IOException("rename failed")
            ImportResult.Ok(target, displayName(asset).replaceFirstChar { it.titlecase() })
        } catch (e: IOException) {
            ImportResult.Error(R.string.error_read)
        }
    }

    /**
     * Copies a document picked with the system file picker and checks that it
     * holds a compatible game (plain ROM, .zip or .7z). Call off the main thread.
     */
    fun import(uri: Uri): ImportResult {
        val resolver = context.contentResolver
        var fileName = "juego"
        var declaredSize = -1L
        try {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                        val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
                        if (nameIndex >= 0 && !cursor.isNull(nameIndex)) fileName = cursor.getString(nameIndex)
                        if (sizeIndex >= 0 && !cursor.isNull(sizeIndex)) declaredSize = cursor.getLong(sizeIndex)
                    }
                }
        } catch (e: Exception) {
            // Some providers fail the metadata query but still open the stream.
        }
        if (declaredSize > MAX_FILE_BYTES) return ImportResult.Error(R.string.error_too_big)

        romsDir.mkdirs()
        val safeName = sanitize(fileName)
        val partial = File(romsDir, "$safeName.part")
        val target = File(romsDir, safeName)
        try {
            val input = resolver.openInputStream(uri) ?: return ImportResult.Error(R.string.error_read)
            input.use { stream ->
                partial.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    while (true) {
                        val read = stream.read(buffer)
                        if (read < 0) break
                        total += read
                        if (total > MAX_FILE_BYTES) {
                            partial.delete()
                            return ImportResult.Error(R.string.error_too_big)
                        }
                        output.write(buffer, 0, read)
                    }
                }
            }
        } catch (e: Exception) {
            partial.delete()
            return ImportResult.Error(R.string.error_read)
        }

        if (isRar(partial)) {
            partial.delete()
            return ImportResult.Error(R.string.error_rar)
        }
        if (Emulator.probe(partial.path) != Emulator.Result.OK) {
            partial.delete()
            return ImportResult.Error(R.string.error_not_a_game)
        }
        target.delete()
        if (!partial.renameTo(target)) {
            partial.delete()
            return ImportResult.Error(R.string.error_read)
        }
        // Keep only the current game: copies can weigh up to 32 MB each.
        romsDir.listFiles()?.filter { it != target }?.forEach { it.delete() }
        return ImportResult.Ok(target, displayName(fileName))
    }

    private fun isRar(file: File): Boolean = try {
        file.inputStream().use { input ->
            val header = ByteArray(4)
            input.read(header) == 4 && header.contentEquals(RAR_MAGIC)
        }
    } catch (e: IOException) {
        false
    }

    companion object {
        private const val KEY_URI = "uri"
        private const val KEY_NAME = "name"
        private const val KEY_PATH = "path"
        // Biggest GBA cartridge is 32 MB; leave room for uncompressed archives.
        private const val MAX_FILE_BYTES = 64L * 1024 * 1024
        private val RAR_MAGIC = byteArrayOf(0x52, 0x61, 0x72, 0x21) // "Rar!"
        val GAME_EXTENSIONS = setOf("gba", "agb", "gb", "gbc", "bin", "zip", "7z")

        /** File name the provider shows for a document, or null. */
        fun queryDisplayName(context: Context, uri: Uri): String? = try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (cursor.moveToFirst() && index >= 0 && !cursor.isNull(index)) cursor.getString(index) else null
                }
        } catch (e: Exception) {
            null
        }

        fun displayName(fileName: String): String {
            val base = fileName.substringAfterLast('/')
            val withoutExtension = if ('.' in base) base.substringBeforeLast('.') else base
            return withoutExtension.replace('_', ' ').trim().ifEmpty { base }
        }

        fun sanitize(fileName: String): String {
            val cleaned = fileName.substringAfterLast('/')
                .replace(Regex("[^A-Za-z0-9._ ()\\[\\]-]"), "_")
                .trim('.', ' ')
                .take(100)
            return cleaned.ifEmpty { "juego" }
        }
    }
}
