package com.nestordelrio.emu

import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Color
import android.os.Build
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import com.nestordelrio.emu.core.Emulator
import com.nestordelrio.emu.core.Keys
import java.io.ByteArrayOutputStream
import java.io.File
import java.text.DateFormat
import java.util.Date

/**
 * The game screen.
 *
 * Portrait: game on top at full width, a quick bar (Turbo, Guardar, Cargar,
 * Menú) and the controls below. Landscape: game as large as the screen allows
 * with translucent controls on top. Back opens the pause menu.
 */
class GameActivity : Activity() {

    companion object {
        const val EXTRA_ROM_PATH = "rom_path"
        const val EXTRA_NAME = "name"
        /** Document the ROM was opened from (optional): the save importer starts in its folder. */
        const val EXTRA_SOURCE_URI = "source_uri"
        private const val REQUEST_IMPORT_SAVE = 10
        private const val REQUEST_EXPORT_SAVE = 11
        private const val REQUEST_SAVE_FOLDER = 12
        private const val KEY_TURBO_HINT = "turbo_hint_shown"
        /** Each tap on Turbo moves to the next speed; after ×8 it turns off. */
        private val TURBO_SPEEDS = intArrayOf(2, 4, 8)
        // Bigger than any battery save: probably a save state.
        private const val MAX_SAVE_BYTES = 256 * 1024
    }

    private lateinit var root: FrameLayout
    private lateinit var surface: GameSurfaceView
    private lateinit var gamepad: GamepadView
    private lateinit var turboChip: TextView
    private lateinit var cheatStore: CheatStore
    private lateinit var store: RomStore
    private lateinit var saveFolder: SaveFolder
    private val settings by lazy { getSharedPreferences("settings", Context.MODE_PRIVATE) }

    private var touchKeys = 0
    private var hardwareKeys = 0
    private var gameName = ""
    private var gameId = ""
    /** File name for this game's save in the saves folder, without ".sav". */
    private var saveBaseName = ""
    private var sourceUri: Uri? = null
    /** Index in [TURBO_SPEEDS], or -1 when turbo is off. */
    private var turboStep = -1
    private var modal: Dialog? = null
    private var pickerOpen = false
    private var backCallback: OnBackInvokedCallback? = null
    private var ready = false
    private var theme = ControlThemes.CLASSIC
    private var landscape = false

    private val turboOn: Boolean
        get() = turboStep >= 0

    private val turboSpeed: Int
        get() = if (turboOn) TURBO_SPEEDS[turboStep] else 1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val romPath = intent.getStringExtra(EXTRA_ROM_PATH)
        if (romPath == null) {
            finish()
            return
        }
        store = RomStore(this)
        val error = when (Emulator.load(romPath, store.savesDir, store.statesDir)) {
            Emulator.Result.OK -> 0
            Emulator.Result.NOT_FOUND -> R.string.error_missing
            Emulator.Result.NOT_A_GAME -> R.string.error_not_a_game
            Emulator.Result.BAD_SAVE, Emulator.Result.FAILED -> R.string.error_start
        }
        if (error != 0) {
            Toast.makeText(this, error, Toast.LENGTH_LONG).show()
            finish()
            return
        }
        gameName = intent.getStringExtra(EXTRA_NAME).orEmpty().ifEmpty { Emulator.title() }
        sourceUri = intent.getStringExtra(EXTRA_SOURCE_URI)?.let(Uri::parse)
        gameId = Emulator.gameId()
        cheatStore = CheatStore(this, gameId)
        applyCheats(cheatStore.load())
        saveFolder = SaveFolder(this)
        // The ROM's own file name, like other emulators; the remembered one when
        // the original file can't be read (after a reinstall).
        saveBaseName = romBaseName() ?: saveFolder.storedName(gameId) ?: gameName
        saveFolder.rememberName(gameId, saveBaseName)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        surface = GameSurfaceView(this)
        gamepad = GamepadView(this).apply {
            onKeysChanged = { keys ->
                touchKeys = keys
                pushKeys()
            }
        }
        turboChip = chip(getString(R.string.game_turbo)) { nextTurboSpeed() }
        root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        setContentView(root)
        applySafeInsets(root, includeSystemBars = false)
        buildLayout(resources.configuration)
        registerBackHandler()
        ready = true
        // Not on recreation: the session already synced.
        if (savedInstanceState == null) syncWithFolder()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (ready) buildLayout(newConfig)
    }

    // ---- Layout -----------------------------------------------------------

    private fun chip(label: String, onClick: () -> Unit) = TextView(this).apply {
        text = label
        gravity = Gravity.CENTER
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        setPadding(dp(14), dp(8), dp(14), dp(8))
        minHeight = dp(40)
        setOnClickListener { onClick() }
    }

    private fun styleChip(chip: TextView, active: Boolean) {
        if (active) {
            chip.background = rounded(theme.accent, dp(20).toFloat())
            chip.setTextColor(theme.onAccent)
        } else {
            chip.background = rounded(theme.chipFill(landscape), dp(20).toFloat(), theme.chipStroke(landscape), dp(1))
            chip.setTextColor(theme.chipText(landscape))
        }
    }

    private fun updateTurboChip() {
        turboChip.text = if (turboOn) getString(R.string.game_turbo_on, turboSpeed) else getString(R.string.game_turbo)
        styleChip(turboChip, turboOn)
    }

    private fun buildLayout(config: Configuration) {
        (surface.parent as? ViewGroup)?.removeView(surface)
        (gamepad.parent as? ViewGroup)?.removeView(gamepad)
        (turboChip.parent as? ViewGroup)?.removeView(turboChip)
        root.removeAllViews()
        // Cheap; also picks up a refund that reset the theme.
        theme = ControlThemes.current(this)
        landscape = config.orientation == Configuration.ORIENTATION_LANDSCAPE
        gamepad.theme = theme
        updateTurboChip()

        val match = ViewGroup.LayoutParams.MATCH_PARENT
        val wrap = ViewGroup.LayoutParams.WRAP_CONTENT
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        fun addChip(view: TextView) {
            styleChip(view, active = view === turboChip && turboOn)
            bar.addView(view, LinearLayout.LayoutParams(wrap, wrap).apply { marginStart = dp(4); marginEnd = dp(4) })
        }

        if (!landscape) {
            // The game uses the full width; sharp-bilinear scaling keeps it crisp
            // at non-integer sizes.
            val (videoW, videoH) = Emulator.videoSize()
            val screenWidth = resources.displayMetrics.widthPixels
            val screenHeight = minOf(
                screenWidth * videoH / videoW,
                (resources.displayMetrics.heightPixels * 0.55f).toInt(),
            )
            addChip(turboChip)
            addChip(chip(getString(R.string.game_save)) { saveState() })
            addChip(chip(getString(R.string.game_load)) { loadState() })
            addChip(chip(getString(R.string.game_menu)) { showMenu() })
            bar.setPadding(dp(8), dp(10), dp(8), dp(4))

            val column = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setBackgroundColor(theme.panel)
            }
            column.addView(surface, LinearLayout.LayoutParams(match, screenHeight))
            column.addView(bar, LinearLayout.LayoutParams(match, wrap))
            column.addView(gamepad, LinearLayout.LayoutParams(match, 0, 1f))
            root.addView(column, FrameLayout.LayoutParams(match, match))
            gamepad.overlay = false
        } else {
            addChip(turboChip)
            addChip(chip(getString(R.string.game_menu)) { showMenu() })
            root.addView(surface, FrameLayout.LayoutParams(match, match))
            root.addView(gamepad, FrameLayout.LayoutParams(match, match))
            root.addView(
                bar,
                FrameLayout.LayoutParams(wrap, wrap, Gravity.TOP or Gravity.CENTER_HORIZONTAL).apply { topMargin = dp(8) },
            )
            gamepad.overlay = true
        }
    }

    // ---- Lifecycle --------------------------------------------------------

    override fun onResume() {
        super.onResume()
        if (!ready) return
        pickerOpen = false
        enterImmersiveMode()
        surface.onResume()
        if (modal?.isShowing != true) Emulator.resume()
    }

    override fun onPause() {
        super.onPause()
        if (!ready) return
        gamepad.reset()
        hardwareKeys = 0
        pushKeys()
        Emulator.pause()
        surface.onPause()
        mirrorToFolder()
        // Lets the home screen suggest a saves folder right after this session.
        if (isFinishing && sourceUri != null) saveFolder.notePlayedOwnGame()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && ready) enterImmersiveMode()
    }

    override fun onDestroy() {
        super.onDestroy()
        val wasReady = ready
        ready = false
        if (!wasReady) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            backCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        }
        modal?.dismiss()
        if (isFinishing) Emulator.unload()
    }

    private fun pushKeys() = Emulator.setKeys(touchKeys or hardwareKeys)

    /** Shows a dialog with the game paused; the game resumes when the last one closes. */
    private fun showModal(dialog: Dialog) {
        gamepad.reset()
        Emulator.pause()
        modal = dialog
        dialog.setOnDismissListener {
            if (modal === dialog) {
                modal = null
                if (ready && !isFinishing && !pickerOpen) {
                    enterImmersiveMode()
                    Emulator.resume()
                }
            }
        }
        dialog.show()
    }

    private fun toast(message: Int) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()

    // ---- Turbo ------------------------------------------------------------

    /** Off → ×2 → ×4 → ×8 → off. */
    private fun nextTurboSpeed() {
        turboStep = if (turboStep + 1 < TURBO_SPEEDS.size) turboStep + 1 else -1
        Emulator.setSpeed(turboSpeed)
        updateTurboChip()
        // ×4 and ×8 run muted: say so once.
        if (turboSpeed > 2 && !settings.getBoolean(KEY_TURBO_HINT, false)) {
            settings.edit().putBoolean(KEY_TURBO_HINT, true).apply()
            Toast.makeText(this, getString(R.string.turbo_muted_hint, turboSpeed), Toast.LENGTH_LONG).show()
        }
    }

    // ---- Pause menu -------------------------------------------------------

    private fun showMenu() {
        if (!ready || modal?.isShowing == true) return
        val actions = mutableListOf<Pair<String, () -> Unit>>(
            getString(R.string.menu_resume) to {},
            getString(R.string.menu_save_state) to { saveState() },
            getString(R.string.menu_load_state) to { loadState() },
            getString(R.string.menu_cheats) to { showCheats() },
            getString(R.string.menu_import_save) to { showImportGuide() },
            getString(R.string.menu_export_save) to { pickExportTarget() },
        )
        if (Emulator.hasSaveBackup()) {
            actions += getString(R.string.menu_restore_save) to { restoreSave() }
        }
        actions += getString(R.string.menu_save_folder) to { showSaveFolder() }
        actions += getString(R.string.menu_exit) to { finish() }

        val dialog = AlertDialog.Builder(this)
            .setTitle(gameName)
            .setItems(actions.map { it.first }.toTypedArray()) { _, which -> actions[which].second() }
            .create()
        showModal(dialog)
    }

    private fun saveState() {
        toast(if (Emulator.saveState()) R.string.state_saved else R.string.state_error)
    }

    private fun loadState() {
        toast(
            when {
                !Emulator.hasState() -> R.string.state_none
                Emulator.loadState() -> R.string.state_loaded
                else -> R.string.error_start
            },
        )
    }

    // ---- Cheats -----------------------------------------------------------

    private fun applyCheats(cheats: List<Cheat>) {
        Emulator.applyCheats(cheats.filter { it.enabled }.map { it.code })
    }

    private fun showCheats() {
        showModal(CheatsDialog(this, cheatStore) { cheats -> applyCheats(cheats) }.create())
    }

    // ---- Saves from other emulators ---------------------------------------

    /** Base name of the ROM file the player opened ("Pokemon Emerald" for "Pokemon Emerald.zip"). */
    private fun romBaseName(): String? =
        sourceUri?.let { RomStore.queryDisplayName(this, it) }?.substringBeforeLast('.')?.takeIf { it.isNotBlank() }

    /** Tells the player which file to look for before opening the picker. */
    private fun showImportGuide() {
        val parts = mutableListOf(getString(R.string.import_guide_body, romBaseName() ?: gameName))
        if (sourceUri != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            parts += getString(R.string.import_guide_folder)
        }
        parts += getString(R.string.import_guide_states)
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.import_guide_title)
            .setMessage(parts.joinToString("\n\n"))
            .setPositiveButton(R.string.import_pick) { _, _ -> pickSaveToImport() }
            .setNegativeButton(R.string.action_cancel, null)
            .create()
        showModal(dialog)
    }

    private fun pickSaveToImport() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            // .sav/.srm have no registered MIME type: providers report them as
            // application/octet-stream. Hides photos, videos and music.
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/*"))
            // Other emulators (My Boy!, VBA...) keep the .sav next to the ROM:
            // start the picker in the folder the game was opened from.
            val source = sourceUri
            if (source != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                putExtra(DocumentsContract.EXTRA_INITIAL_URI, source)
            }
        }
        launchPicker(intent, REQUEST_IMPORT_SAVE)
    }

    private fun pickExportTarget() {
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_TITLE, "${RomStore.sanitize(gameName)}.sav")
        }
        launchPicker(intent, REQUEST_EXPORT_SAVE)
    }

    private fun launchPicker(intent: Intent, request: Int, error: Int = R.string.import_read_error) {
        pickerOpen = true
        try {
            startActivityForResult(intent, request)
        } catch (e: Exception) {
            pickerOpen = false
            toast(error)
        }
    }

    @Deprecated("Framework Activity result API; this app has no AndroidX dependency")
    @Suppress("OVERRIDE_DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        pickerOpen = false
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null || !ready) return
        when (requestCode) {
            REQUEST_IMPORT_SAVE -> {
                val bytes = try {
                    readLimited(uri, MAX_SAVE_BYTES + 1)
                } catch (e: Exception) {
                    null
                }
                when {
                    bytes == null -> toast(R.string.import_read_error)
                    bytes.size > MAX_SAVE_BYTES -> Toast.makeText(this, R.string.import_state, Toast.LENGTH_LONG).show()
                    else -> confirmImport(bytes, RomStore.queryDisplayName(this, uri))
                }
            }
            REQUEST_EXPORT_SAVE -> exportSave(uri)
            REQUEST_SAVE_FOLDER -> useSaveFolder(uri)
        }
    }

    private fun readLimited(uri: Uri, limit: Int): ByteArray? {
        val input = contentResolver.openInputStream(uri) ?: return null
        input.use { stream ->
            val out = ByteArrayOutputStream()
            val buffer = ByteArray(16 * 1024)
            while (out.size() < limit) {
                val read = stream.read(buffer)
                if (read < 0) break
                out.write(buffer, 0, read)
            }
            return out.toByteArray()
        }
    }

    private fun confirmImport(bytes: ByteArray, fileName: String?) {
        val parts = mutableListOf<String>()
        if (fileName != null) parts += getString(R.string.import_confirm_file, fileName)
        parts += getString(R.string.import_confirm_body, gameName)
        val expected = romBaseName()
        if (fileName != null && expected != null && !sameBaseName(fileName, expected)) {
            parts += getString(R.string.import_name_mismatch)
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.import_confirm_title)
            .setMessage(parts.joinToString("\n\n"))
            .setPositiveButton(R.string.import_button) { _, _ ->
                when (Emulator.importSave(bytes)) {
                    Emulator.Result.OK -> toast(R.string.import_ok)
                    else -> Toast.makeText(this, R.string.import_bad, Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .create()
        showModal(dialog)
    }

    /** "Pokemon Emerald.sav" vs "Pokemon_Emerald" → same game, ignoring case, symbols and extension. */
    private fun sameBaseName(fileName: String, romBase: String): Boolean {
        fun normalize(value: String) = value.substringBeforeLast('.').lowercase().filter { it.isLetterOrDigit() }
        val a = normalize(fileName)
        val b = romBase.lowercase().filter { it.isLetterOrDigit() }
        return a.isNotEmpty() && b.isNotEmpty() && (a == b || a.startsWith(b) || b.startsWith(a))
    }

    private fun exportSave(uri: Uri) {
        val bytes = Emulator.exportSave()
        if (bytes == null) {
            toast(R.string.export_none)
            return
        }
        val ok = try {
            contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } != null
        } catch (e: Exception) {
            false
        }
        toast(if (ok) R.string.export_ok else R.string.export_error)
    }

    private fun restoreSave() {
        toast(if (Emulator.restoreSaveBackup()) R.string.restore_ok else R.string.restore_error)
    }

    // ---- Saves folder -----------------------------------------------------

    /** At start: brings in the folder's save when only it changed, or asks when both did. */
    private fun syncWithFolder() {
        if (saveFolder.folder() == null) return
        val id = gameId
        val base = saveBaseName
        val appData = Emulator.exportSave()
        val appModified = File(store.savesDir, "$id.sav").lastModified()
        SaveFolder.io.execute {
            val plan = saveFolder.plan(id, base, appData, appModified)
            val folderName = if (plan is SaveFolder.Plan.Nothing) "" else saveFolder.displayName()
            runOnUiThread {
                if (ready && !isFinishing && Emulator.gameId() == id) applyPlan(plan, folderName)
            }
        }
    }

    private fun applyPlan(plan: SaveFolder.Plan, folderName: String) {
        when (plan) {
            is SaveFolder.Plan.Nothing -> {}
            is SaveFolder.Plan.Load -> if (loadFromFolder(plan.data, plan.folderSum)) {
                Toast.makeText(this, getString(R.string.save_folder_restored, folderName), Toast.LENGTH_LONG).show()
            }
            is SaveFolder.Plan.Ask -> askWhichSave(plan)
        }
    }

    /** Loads the folder's save; the app's goes to "previous save" in the menu, like an import. */
    private fun loadFromFolder(data: ByteArray, folderSum: Long): Boolean {
        if (Emulator.importSave(data) != Emulator.Result.OK) return false
        val id = gameId
        val now = Emulator.exportSave()
        SaveFolder.io.execute { saveFolder.loaded(id, now, folderSum) }
        return true
    }

    private fun askWhichSave(plan: SaveFolder.Plan.Ask) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        fun date(millis: Long) = if (millis > 0) format.format(Date(millis)) else "?"
        val message = getString(
            R.string.save_conflict_body,
            gameName,
            date(plan.folderModified),
            date(plan.appModified),
            getString(R.string.menu_restore_save),
        )
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.save_conflict_title)
            .setMessage(message)
            .setCancelable(false)
            .setPositiveButton(R.string.save_conflict_folder) { _, _ ->
                if (loadFromFolder(plan.folderData, plan.folderSum)) {
                    toast(R.string.import_ok)
                } else {
                    Toast.makeText(this, R.string.import_bad, Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.save_conflict_app) { _, _ -> keepAppSave(plan.folderData) }
            .create()
        showModal(dialog)
    }

    /** The app's save stays and replaces the folder's; the folder's becomes "previous save". */
    private fun keepAppSave(folderData: ByteArray) {
        // Same slot the native side uses when importing (emu_core.c, g_backupPath).
        val backup = File(store.savesDir, "backup/$gameId.sav")
        val partial = File(backup.path + ".part")
        try {
            backup.parentFile?.mkdirs()
            partial.writeBytes(folderData)
            if (!partial.renameTo(backup)) partial.delete()
        } catch (e: Exception) {
            partial.delete()
        }
        val appData = Emulator.exportSave() ?: return
        val id = gameId
        val base = saveBaseName
        SaveFolder.io.execute { saveFolder.keepApp(id, base, appData) }
    }

    /** After playing: copies the save to the folder (skipped when the folder's changed meanwhile). */
    private fun mirrorToFolder() {
        if (saveFolder.folder() == null) return
        val data = Emulator.exportSave() ?: return
        val id = gameId
        val base = saveBaseName
        SaveFolder.io.execute { saveFolder.afterSession(id, base, data) }
    }

    private fun showSaveFolder() {
        if (saveFolder.folder() == null) {
            val again = saveFolder.needsReselect()
            val dialog = AlertDialog.Builder(this)
                .setTitle(if (again) R.string.save_folder_again_title else R.string.save_folder_prompt_title)
                .setMessage(if (again) R.string.save_folder_again_body else R.string.save_folder_prompt_body)
                .setPositiveButton(R.string.save_folder_choose) { _, _ -> pickSaveFolder() }
                .setNegativeButton(R.string.save_folder_later, null)
                .create()
            showModal(dialog)
            return
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.menu_save_folder)
            .setMessage(getString(R.string.save_folder_current, saveFolder.displayName()))
            .setPositiveButton(R.string.save_folder_change) { _, _ -> pickSaveFolder() }
            .setNeutralButton(R.string.save_folder_disable) { _, _ ->
                saveFolder.disable()
                toast(R.string.save_folder_off)
            }
            .setNegativeButton(R.string.save_folder_close, null)
            .create()
        showModal(dialog)
    }

    /** Starts in the folder chosen before, or in the game's folder. */
    private fun pickSaveFolder() {
        val intent = saveFolder.pickerIntent(saveFolder.storedUri() ?: sourceUri)
        launchPicker(intent, REQUEST_SAVE_FOLDER, R.string.save_folder_error)
    }

    private fun useSaveFolder(tree: Uri) {
        if (!saveFolder.set(tree)) {
            toast(R.string.save_folder_error)
            return
        }
        Toast.makeText(this, getString(R.string.save_folder_set, saveFolder.displayName()), Toast.LENGTH_LONG).show()
        syncWithFolder()
        val saves = store.savesDir
        SaveFolder.io.execute { saveFolder.copyAll(saves) }
    }

    // ---- Back -------------------------------------------------------------

    private fun registerBackHandler() {
        // Android 16 with targetSdk 36 never calls onBackPressed.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val callback = OnBackInvokedCallback { showMenu() }
            onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback)
            backCallback = callback
        }
    }

    @Deprecated("Used only below Android 13; see registerBackHandler()")
    @Suppress("OVERRIDE_DEPRECATION")
    override fun onBackPressed() {
        if (ready) showMenu() else finish()
    }

    // ---- Keyboards and controllers ---------------------------------------

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val key = keyFor(event.keyCode)
        if (key == 0 || !ready) return super.dispatchKeyEvent(event)
        when (event.action) {
            KeyEvent.ACTION_DOWN -> hardwareKeys = hardwareKeys or key
            KeyEvent.ACTION_UP -> hardwareKeys = hardwareKeys and key.inv()
        }
        pushKeys()
        return true
    }

    /** Controllers by button label; keyboard for testing in the Android emulator. */
    private fun keyFor(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> Keys.UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> Keys.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> Keys.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> Keys.RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K -> Keys.A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_J -> Keys.B
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> Keys.L
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> Keys.R
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> Keys.START
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_DEL -> Keys.SELECT
        else -> 0
    }
}
