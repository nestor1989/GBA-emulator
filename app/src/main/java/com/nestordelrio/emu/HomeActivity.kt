package com.nestordelrio.emu

import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.nestordelrio.emu.core.Emulator
import java.io.File
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * First screen. One obvious action at a time: "Continue" when there is a last
 * game, "Play Cometa" (the bundled game) on a first launch, "Open game"
 * otherwise. No settings, no onboarding.
 */
class HomeActivity : Activity(), Support.Listener {

    private lateinit var store: RomStore
    private lateinit var content: LinearLayout
    private lateinit var saveFolder: SaveFolder
    private var folderPrompt: Dialog? = null
    private var pickingFolder = false
    private val io: ExecutorService = Executors.newSingleThreadExecutor()
    private var busy = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = RomStore(this)
        saveFolder = SaveFolder(this)
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(dp(24), dp(32), dp(24), dp(24))
        }
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            addView(
                content,
                ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
            )
        }
        setContentView(scroll)
        applySafeInsets(scroll, includeSystemBars = true)
        Support.init(this)
    }

    override fun onStart() {
        super.onStart()
        Support.addListener(this)
    }

    override fun onResume() {
        super.onResume()
        render()
        // Restores the pack after a reinstall, finishes payments made while the
        // app was closed and notices refunds. Local to Play: no network wait.
        Support.refresh()
        suggestSaveFolder()
    }

    override fun onStop() {
        super.onStop()
        Support.removeListener(this)
    }

    override fun onSupportChanged() {
        if (!isDestroyed) render()
    }

    override fun onSupportEvent(event: Support.Event) {
        val message = when (event) {
            Support.Event.THANKS_PACK -> R.string.support_thanks_pack
            Support.Event.THANKS_TIP -> R.string.support_thanks_tip
            else -> return
        }
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    override fun onDestroy() {
        super.onDestroy()
        folderPrompt?.dismiss()
        io.shutdown()
    }

    private fun render() {
        content.removeAllViews()

        content.addView(TextView(this).apply {
            text = getString(R.string.app_name)
            setTextColor(getColor(R.color.text))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 34f)
            typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
            gravity = Gravity.CENTER
        })
        content.addView(TextView(this).apply {
            text = getString(R.string.home_subtitle)
            setTextColor(getColor(R.color.text_muted))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, dp(40))
        })
        val supporter = Support.isSupporter(this)
        if (supporter) {
            content.addView(
                TextView(this).apply {
                    text = getString(R.string.home_supporter_badge)
                    setTextColor(getColor(R.color.supporter))
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                    typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
                    background = rounded(0, dp(12).toFloat(), getColor(R.color.supporter), dp(1))
                    setPadding(dp(12), dp(4), dp(12), dp(4))
                    setOnClickListener { openSupport() }
                },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                    gravity = Gravity.CENTER_HORIZONTAL
                    topMargin = -dp(28)
                    bottomMargin = dp(28)
                },
            )
        }

        val recent = store.recent()
        val bundled = store.bundledGame()
        var primaryUsed = false

        if (recent != null) {
            addButton(getString(R.string.home_continue, recent.name), primary = true) { continueRecent(recent) }
            primaryUsed = true
        }
        if (bundled != null && recent == null) {
            addButton(getString(R.string.home_play_bundled), primary = true) { playBundled(bundled) }
            primaryUsed = true
        }
        addButton(
            if (busy) getString(R.string.home_opening) else getString(R.string.home_open),
            primary = !primaryUsed,
        ) { openPicker(allFiles = false) }
        content.addView(TextView(this).apply {
            text = getString(R.string.home_all_files)
            setTextColor(getColor(R.color.text_muted))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(10), dp(16), dp(4))
            setOnClickListener { if (!busy) openPicker(allFiles = true) }
        })
        if (bundled != null && recent != null && !store.isBundled(recent.path)) {
            addButton(getString(R.string.home_play_bundled), primary = false) { playBundled(bundled) }
        }

        content.addView(TextView(this).apply {
            text = getString(if (supporter) R.string.home_customize else R.string.home_support)
            setTextColor(getColor(if (supporter) R.color.text_muted else R.color.supporter))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(36), dp(16), dp(4))
            setOnClickListener { if (!busy) openSupport() }
        })
        content.addView(TextView(this).apply {
            text = getString(R.string.home_licenses)
            setTextColor(getColor(R.color.text_muted))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(16), dp(16), dp(16))
            setOnClickListener { startActivity(Intent(this@HomeActivity, LicensesActivity::class.java)) }
        })
    }

    private fun addButton(label: String, primary: Boolean, onClick: () -> Unit) {
        val button = Button(this).apply {
            text = label
            isAllCaps = false
            setTextSize(TypedValue.COMPLEX_UNIT_SP, if (primary) 20f else 17f)
            setTextColor(getColor(R.color.text))
            background = if (primary) {
                rounded(getColor(R.color.accent), dp(16).toFloat())
            } else {
                rounded(getColor(R.color.surface), dp(16).toFloat(), getColor(R.color.text_muted), dp(1))
            }
            stateListAnimator = null
            minHeight = dp(if (primary) 72 else 60)
            setPadding(dp(20), dp(12), dp(20), dp(12))
            isEnabled = !busy
            alpha = if (busy) 0.6f else 1f
            setOnClickListener { if (!busy) onClick() }
        }
        val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        params.topMargin = dp(12)
        params.width = minOf(resources.displayMetrics.widthPixels - dp(48), dp(420))
        content.addView(button, params)
    }

    // ---- Actions ------------------------------------------------------------

    private fun openSupport() = startActivity(Intent(this, SupportActivity::class.java))

    /**
     * Android's picker can only filter by MIME type, and .gba/.gb/.gbc have no
     * registered one: providers report them as application/octet-stream. So the
     * filter keeps "unknown binary" files and archives, and hides photos,
     * videos, music, documents and apps. The content is checked after picking.
     */
    private fun openPicker(allFiles: Boolean) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            if (!allFiles) putExtra(Intent.EXTRA_MIME_TYPES, ROM_MIME_TYPES)
        }
        try {
            startActivityForResult(intent, REQUEST_OPEN)
        } catch (e: Exception) {
            Toast.makeText(this, R.string.error_read, Toast.LENGTH_LONG).show()
        }
    }

    // ---- Saves folder ---------------------------------------------------------

    /**
     * Suggests a saves folder after the first sessions with the player's own
     * games, and once after a reinstall when a folder was in use. Never before
     * playing: the first game should start right away.
     */
    private fun suggestSaveFolder() {
        if (busy || pickingFolder || folderPrompt?.isShowing == true) return
        val prompt = saveFolder.promptToShow()
        if (prompt == SaveFolder.Prompt.NONE) return
        saveFolder.promptShown(prompt)
        val again = prompt == SaveFolder.Prompt.AGAIN
        // The picker starts in the old folder, or in the last game's folder.
        val initial = if (again) saveFolder.storedUri() else store.recent()?.uri
        folderPrompt = AlertDialog.Builder(this)
            .setTitle(if (again) R.string.save_folder_again_title else R.string.save_folder_prompt_title)
            .setMessage(if (again) R.string.save_folder_again_body else R.string.save_folder_prompt_body)
            .setPositiveButton(R.string.save_folder_choose) { _, _ -> pickSaveFolder(initial) }
            .setNegativeButton(R.string.save_folder_later, null)
            .show()
    }

    private fun pickSaveFolder(initial: Uri?) {
        pickingFolder = true
        try {
            startActivityForResult(saveFolder.pickerIntent(initial), REQUEST_SAVE_FOLDER)
        } catch (e: Exception) {
            pickingFolder = false
            Toast.makeText(this, R.string.save_folder_error, Toast.LENGTH_LONG).show()
        }
    }

    private fun useSaveFolder(tree: Uri) {
        if (!saveFolder.set(tree)) {
            Toast.makeText(this, R.string.save_folder_error, Toast.LENGTH_LONG).show()
            return
        }
        Toast.makeText(this, getString(R.string.save_folder_set, saveFolder.displayName()), Toast.LENGTH_LONG).show()
        // Saves of games played before go to the folder now; each game brings
        // its folder save back when it is opened.
        val saves = store.savesDir
        SaveFolder.io.execute { saveFolder.copyAll(saves) }
    }

    @Deprecated("Framework Activity result API; this app has no AndroidX dependency")
    @Suppress("OVERRIDE_DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_SAVE_FOLDER) {
            pickingFolder = false
            val tree = data?.data
            if (resultCode == RESULT_OK && tree != null) useSaveFolder(tree)
            return
        }
        if (requestCode != REQUEST_OPEN || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            // Not every provider grants persistable access; the local copy still works.
        }
        importAndPlay(uri)
    }

    private fun importAndPlay(uri: Uri) {
        runInBackground({ store.import(uri) }) { result ->
            if (result is RomStore.ImportResult.Ok) {
                store.saveRecent(uri, result.name, result.file.path)
                launchGame(result.file, result.name, uri)
            }
        }
    }

    private fun playBundled(asset: String) {
        runInBackground({ store.prepareBundled(asset) }) { result ->
            if (result is RomStore.ImportResult.Ok) {
                store.saveRecent(null, result.name, result.file.path)
                launchGame(result.file, result.name, null)
            }
        }
    }

    private fun continueRecent(recent: RomStore.Recent) {
        val bundled = store.bundledGame()
        if (bundled != null && store.isBundled(recent.path)) {
            playBundled(bundled)
            return
        }
        val file = File(recent.path)
        if (file.isFile) {
            launchGame(file, recent.name, recent.uri)
            return
        }
        // The local copy is gone (e.g. app data cleared): copy it again through
        // the persisted permission.
        val uri = recent.uri
        if (uri == null) {
            Toast.makeText(this, R.string.error_missing, Toast.LENGTH_LONG).show()
            return
        }
        runInBackground({
            try {
                store.import(uri)
            } catch (e: SecurityException) {
                RomStore.ImportResult.Error(R.string.error_missing)
            }
        }) { result ->
            if (result is RomStore.ImportResult.Ok) {
                store.saveRecent(uri, recent.name, result.file.path)
                launchGame(result.file, recent.name, uri)
            }
        }
    }

    private fun runInBackground(
        task: () -> RomStore.ImportResult,
        onDone: (RomStore.ImportResult) -> Unit,
    ) {
        busy = true
        render()
        io.execute {
            val result = try {
                task()
            } catch (e: Exception) {
                RomStore.ImportResult.Error(R.string.error_read)
            }
            runOnUiThread {
                busy = false
                if (isDestroyed) return@runOnUiThread
                if (result is RomStore.ImportResult.Error) {
                    Toast.makeText(this, result.messageRes, Toast.LENGTH_LONG).show()
                }
                render()
                onDone(result)
            }
        }
    }

    /** [source] is the document the player picked; used to open the save importer next to it. */
    private fun launchGame(file: File, name: String, source: Uri?) {
        // A new pick may reuse a file name: always start from a clean session.
        Emulator.unload()
        startActivity(
            Intent(this, GameActivity::class.java)
                .putExtra(GameActivity.EXTRA_ROM_PATH, file.path)
                .putExtra(GameActivity.EXTRA_NAME, name)
                .putExtra(GameActivity.EXTRA_SOURCE_URI, source?.toString()),
        )
    }

    private companion object {
        const val REQUEST_OPEN = 1
        const val REQUEST_SAVE_FOLDER = 2
        val ROM_MIME_TYPES = arrayOf(
            "application/octet-stream",
            "application/zip",
            "application/x-zip",
            "application/x-zip-compressed",
            "application/x-7z-compressed",
            "application/x-gba-rom",
            "application/x-gameboy-rom",
            "application/x-gameboy-color-rom",
        )
    }
}
