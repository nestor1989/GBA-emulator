package com.nestordelrio.emu

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.zip.CRC32

/**
 * Optional second home for in-game saves: a folder the player picks, usually
 * the one with their games. Each save is copied there as "<game name>.sav",
 * the name other emulators use, so it survives uninstalling the app and moves
 * between emulators in both directions. The app's private copy
 * (files/saves/<CRC>.sav) is still the one the emulator runs on.
 *
 * Sync rule for each game, using checksums of what each side held the last
 * time both matched:
 * - only one side has a save: copy it to the other;
 * - only the folder changed since: load it (the player used another emulator);
 * - only the app changed since: write it to the folder;
 * - both changed, or they never matched: ask which one to keep.
 *
 * File access goes through [io], one thread, so syncs never interleave.
 */
class SaveFolder(context: Context) {

    sealed interface Plan {
        object Nothing : Plan
        /** Load the folder's save into the game. */
        class Load(val data: ByteArray, val folderSum: Long) : Plan
        /** Both sides changed: the player decides. */
        class Ask(val folderData: ByteArray, val folderSum: Long, val folderModified: Long, val appModified: Long) : Plan
    }

    private class Remote(val uri: Uri, val modified: Long)

    private val app = context.applicationContext
    private val resolver = app.contentResolver
    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private val prompts = app.getSharedPreferences(PROMPTS_PREFS, Context.MODE_PRIVATE)

    // ---- Folder choice ---------------------------------------------------------

    /** The folder chosen before, even if its permission is gone (it is not restored after a reinstall). */
    fun storedUri(): Uri? = prefs.getString(KEY_TREE, null)?.let(Uri::parse)

    /** The folder, when set and still writable. */
    fun folder(): Uri? {
        val uri = storedUri() ?: return null
        val granted = resolver.persistedUriPermissions.any {
            it.uri == uri && it.isReadPermission && it.isWritePermission
        }
        return if (granted) uri else null
    }

    /** A folder was chosen before but cannot be used now, typically after reinstalling. */
    fun needsReselect(): Boolean = storedUri() != null && folder() == null

    /** [initial]: where the picker starts, a document (the game's file) or a folder chosen before. */
    fun pickerIntent(initial: Uri?): Intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
        addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or Intent.FLAG_GRANT_PREFIX_URI_PERMISSION,
        )
        if (initial != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // The picker wants a document: a folder chosen before is stored as a tree.
            val start = if (DocumentsContract.isTreeUri(initial)) {
                DocumentsContract.buildDocumentUriUsingTree(initial, DocumentsContract.getTreeDocumentId(initial))
            } else {
                initial
            }
            putExtra(DocumentsContract.EXTRA_INITIAL_URI, start)
        }
    }

    /** Keeps a folder picked with [pickerIntent]. Returns false when it cannot be used. */
    fun set(tree: Uri): Boolean {
        try {
            resolver.takePersistableUriPermission(tree, RW)
        } catch (e: SecurityException) {
            return false
        }
        val old = storedUri()
        if (old != null && old != tree) release(old)
        val editor = prefs.edit().putString(KEY_TREE, tree.toString())
        // Picking the same folder again (after a reinstall) keeps the records:
        // they tell which side changed while the app was gone.
        if (old != tree) clearSyncRecords(editor)
        editor.apply()
        return true
    }

    fun disable() {
        storedUri()?.let(::release)
        val editor = prefs.edit().remove(KEY_TREE)
        clearSyncRecords(editor)
        editor.apply()
    }

    // ---- When to suggest it ------------------------------------------------------
    // Kept in their own preferences file, left out of Auto Backup, so a
    // reinstall asks again.

    enum class Prompt { NONE, FIRST, AGAIN }

    /** After closing a game the player opened from their files. */
    fun notePlayedOwnGame() {
        if (storedUri() == null) prompts.edit().putBoolean(KEY_PLAYED, true).apply()
    }

    /**
     * What the home screen should suggest: [Prompt.AGAIN] once when the folder
     * chosen before lost its permission (a reinstall), [Prompt.FIRST] after
     * playing one of the player's own games, at most twice.
     */
    fun promptToShow(): Prompt = when {
        folder() != null -> Prompt.NONE
        needsReselect() -> if (prompts.getBoolean(KEY_ASKED_AGAIN, false)) Prompt.NONE else Prompt.AGAIN
        prompts.getBoolean(KEY_PLAYED, false) && prompts.getInt(KEY_ASKED, 0) < MAX_FIRST_PROMPTS -> Prompt.FIRST
        else -> Prompt.NONE
    }

    fun promptShown(prompt: Prompt) {
        when (prompt) {
            Prompt.NONE -> {}
            Prompt.AGAIN -> prompts.edit().putBoolean(KEY_ASKED_AGAIN, true).apply()
            Prompt.FIRST -> prompts.edit()
                .putInt(KEY_ASKED, prompts.getInt(KEY_ASKED, 0) + 1)
                .putBoolean(KEY_PLAYED, false)
                .apply()
        }
    }

    /** Name of the folder as the file picker shows it. Call off the main thread when possible. */
    fun displayName(): String {
        val tree = folder() ?: return ""
        val doc = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        return queryName(doc) ?: tree.lastPathSegment?.substringAfterLast(':')?.substringAfterLast('/').orEmpty()
    }

    /** Remembers the file name used for a game, so [copyAll] can name saves of games not open right now. */
    fun rememberName(gameId: String, baseName: String) {
        if (storedName(gameId) != baseName) prefs.edit().putString(KEY_NAME + gameId, baseName).apply()
    }

    /** The name remembered for a game; survives a reinstall through Auto Backup. */
    fun storedName(gameId: String): String? = prefs.getString(KEY_NAME + gameId, null)

    // ---- Sync (call on [io]) -----------------------------------------------------------

    /** Decides what to do when a game starts. May write the app's save to the folder itself. */
    fun plan(gameId: String, baseName: String, appData: ByteArray?, appModified: Long): Plan {
        val tree = folder() ?: return Plan.Nothing
        val remote = find(tree, baseName)
        val folderData = remote?.let(::read)
        val appHas = hasProgress(appData)
        val folderHas = hasProgress(folderData)
        if (!appHas && !folderHas) return Plan.Nothing
        if (!appHas) return Plan.Load(folderData!!, checksum(folderData))
        val appSum = checksum(appData!!)
        if (!folderHas) {
            if (write(tree, baseName, appData, remote)) record(gameId, appSum, appSum)
            return Plan.Nothing
        }
        val folderSum = checksum(folderData!!)
        val record = record(gameId)
        if (record == null) {
            if (appSum == folderSum) {
                record(gameId, appSum, folderSum)
                return Plan.Nothing
            }
            return Plan.Ask(folderData, folderSum, remote!!.modified, appModified)
        }
        val appChanged = appSum != record.first
        val folderChanged = folderSum != record.second
        return when {
            !appChanged && !folderChanged -> Plan.Nothing
            !appChanged -> Plan.Load(folderData, folderSum)
            !folderChanged -> {
                if (write(tree, baseName, appData, remote)) record(gameId, appSum, appSum)
                Plan.Nothing
            }
            else -> Plan.Ask(folderData, folderSum, remote!!.modified, appModified)
        }
    }

    /** After the folder's save was loaded: [appData] is how the app holds it now (it may be trimmed). */
    fun loaded(gameId: String, appData: ByteArray?, folderSum: Long) {
        if (appData != null) record(gameId, checksum(appData), folderSum)
    }

    /** The player kept the app's save in a conflict: it replaces the folder's. */
    fun keepApp(gameId: String, baseName: String, appData: ByteArray) {
        val tree = folder() ?: return
        if (write(tree, baseName, appData, find(tree, baseName))) {
            val sum = checksum(appData)
            record(gameId, sum, sum)
        }
    }

    /**
     * Copies the app's save to the folder after playing, unless the folder's
     * copy changed in the meantime (then the next start asks).
     */
    fun afterSession(gameId: String, baseName: String, appData: ByteArray?) {
        val tree = folder() ?: return
        if (!hasProgress(appData)) return
        val appSum = checksum(appData!!)
        val record = record(gameId)
        if (record != null && record.first == appSum) return
        val remote = find(tree, baseName)
        val folderData = remote?.let(::read)
        if (hasProgress(folderData)) {
            val folderSum = checksum(folderData!!)
            val changedElsewhere = if (record == null) folderSum != appSum else folderSum != record.second
            if (changedElsewhere) return
        }
        if (write(tree, baseName, appData, remote)) record(gameId, appSum, appSum)
    }

    /** Right after choosing a folder: copies the saves of every game played before. */
    fun copyAll(savesDir: File) {
        val tree = folder() ?: return
        val files = savesDir.listFiles { f -> f.isFile && f.name.endsWith(".sav") } ?: return
        for (file in files) {
            val gameId = file.name.removeSuffix(".sav")
            val baseName = storedName(gameId) ?: continue
            if (record(gameId) != null) continue
            val data = runCatching { file.readBytes() }.getOrNull()
            if (!hasProgress(data)) continue
            val appSum = checksum(data!!)
            val remote = find(tree, baseName)
            val folderData = remote?.let(::read)
            when {
                !hasProgress(folderData) -> if (write(tree, baseName, data, remote)) record(gameId, appSum, appSum)
                checksum(folderData!!) == appSum -> record(gameId, appSum, appSum)
                // Different saves on each side: the game asks when it is opened.
            }
        }
    }

    // ---- Records -------------------------------------------------------------------------

    private fun record(gameId: String): Pair<Long, Long>? {
        val value = prefs.getString(KEY_SYNC + gameId, null) ?: return null
        val parts = value.split(':')
        if (parts.size != 2) return null
        return Pair(parts[0].toLongOrNull() ?: return null, parts[1].toLongOrNull() ?: return null)
    }

    private fun record(gameId: String, appSum: Long, folderSum: Long) {
        prefs.edit().putString(KEY_SYNC + gameId, "$appSum:$folderSum").apply()
    }

    private fun clearSyncRecords(editor: android.content.SharedPreferences.Editor) {
        for (key in prefs.all.keys) if (key.startsWith(KEY_SYNC)) editor.remove(key)
    }

    // ---- Documents ---------------------------------------------------------------------------

    private fun release(uri: Uri) {
        runCatching { resolver.releasePersistableUriPermission(uri, RW) }
    }

    /**
     * Same name other emulators use, accents included ("Pokémon Esmeralda.sav"):
     * only characters that storage rejects are replaced.
     */
    private fun fileName(baseName: String): String {
        val cleaned = buildString {
            for (c in baseName) append(if (c < ' ' || c in FORBIDDEN) '_' else c)
        }.trim(' ', '.').take(120)
        return cleaned.ifEmpty { "game" } + ".sav"
    }

    private fun queryName(doc: Uri): String? = try {
        resolver.query(doc, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) else null
        }
    } catch (e: Exception) {
        null
    }

    private fun find(tree: Uri, baseName: String): Remote? {
        val wanted = fileName(baseName)
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        )
        return try {
            resolver.query(children, columns, null, null, null)?.use { c ->
                while (c.moveToNext()) {
                    val name = c.getString(1) ?: continue
                    if (c.getString(3) == DocumentsContract.Document.MIME_TYPE_DIR) continue
                    if (name.equals(wanted, ignoreCase = true)) {
                        val modified = if (c.isNull(2)) 0L else c.getLong(2)
                        return Remote(DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0)), modified)
                    }
                }
                null
            }
        } catch (e: Exception) {
            null
        }
    }

    private fun read(remote: Remote): ByteArray? = try {
        resolver.openInputStream(remote.uri)?.use { input ->
            val out = ByteArrayOutputStream()
            val buffer = ByteArray(16 * 1024)
            while (out.size() <= MAX_SAVE_BYTES) {
                val n = input.read(buffer)
                if (n < 0) break
                out.write(buffer, 0, n)
            }
            if (out.size() > MAX_SAVE_BYTES) null else out.toByteArray()
        }
    } catch (e: Exception) {
        null
    }

    private fun write(tree: Uri, baseName: String, data: ByteArray, existing: Remote?): Boolean = try {
        val target = existing?.uri ?: DocumentsContract.createDocument(
            resolver,
            DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)),
            "application/octet-stream",
            fileName(baseName),
        )
        target != null && resolver.openOutputStream(target, "wt")?.use { it.write(data) } != null
    } catch (e: Exception) {
        false
    }

    companion object {
        private const val PREFS = "save_folder"
        private const val KEY_TREE = "tree"
        private const val KEY_NAME = "name."
        private const val KEY_SYNC = "sync."
        /** Excluded from Auto Backup in res/xml: keep the names in sync. */
        private const val PROMPTS_PREFS = "save_folder_prompts"
        private const val KEY_PLAYED = "played_own_game"
        private const val KEY_ASKED = "first_prompts"
        private const val KEY_ASKED_AGAIN = "again_prompt"
        private const val MAX_FIRST_PROMPTS = 2
        private const val RW = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        // Bigger than any battery save (RetroArch pads to 128 KB).
        private const val MAX_SAVE_BYTES = 256 * 1024
        private const val FORBIDDEN = "/\\:*?\"<>|"

        /** One thread for every folder access in the app. */
        val io: ExecutorService = Executors.newSingleThreadExecutor()

        fun checksum(data: ByteArray): Long = CRC32().apply { update(data) }.value

        /** A save that is only 0xFF or 0x00 bytes is a fresh cartridge, not progress. */
        fun hasProgress(data: ByteArray?): Boolean {
            if (data == null || data.isEmpty()) return false
            var ff = true
            var zero = true
            for (b in data) {
                if (b != 0xFF.toByte()) ff = false
                if (b != 0.toByte()) zero = false
                if (!ff && !zero) return true
            }
            return false
        }
    }
}
