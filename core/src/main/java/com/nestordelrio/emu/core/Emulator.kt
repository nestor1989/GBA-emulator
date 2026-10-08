package com.nestordelrio.emu.core

import java.io.File

/**
 * Process-wide emulator session. Call everything from the main thread, except
 * [probe], which may run on a background thread.
 */
object Emulator {

    enum class Result { OK, NOT_FOUND, NOT_A_GAME, BAD_SAVE, FAILED }

    /** Path of the loaded ROM file, or null when nothing is loaded. */
    var loadedRom: String? = null
        private set

    /** Checks that a file (plain ROM, .zip or .7z) contains a compatible game. */
    fun probe(path: String): Result = toResult(NativeBridge.nativeProbe(path))

    /**
     * Loads a game and starts the emulation thread paused. Loading the same
     * path again is a no-op, so a recreated Activity keeps playing.
     */
    fun load(romPath: String, saveDir: File, stateDir: File): Result {
        if (loadedRom == romPath) return Result.OK
        unload()
        saveDir.mkdirs()
        stateDir.mkdirs()
        val result = toResult(
            NativeBridge.nativeLoad(romPath, saveDir.absolutePath, stateDir.absolutePath)
        )
        if (result == Result.OK) {
            loadedRom = romPath
            NativeBridge.nativeStart()
        }
        return result
    }

    fun resume() {
        if (loadedRom == null) return
        NativeBridge.nativeAudioStart()
        NativeBridge.nativeSetPaused(false)
    }

    /** Pauses emulation and audio, and forces the in-game save to disk. */
    fun pause() {
        if (loadedRom == null) return
        NativeBridge.nativeSetPaused(true)
        NativeBridge.nativeAudioStop()
        NativeBridge.nativeFlushSave()
    }

    fun unload() {
        if (loadedRom == null) return
        NativeBridge.nativeAudioStop()
        NativeBridge.nativeUnload()
        loadedRom = null
    }

    fun setKeys(keys: Int) = NativeBridge.nativeSetKeys(keys)

    fun saveState(): Boolean = loadedRom != null && NativeBridge.nativeSaveState()

    fun loadState(): Boolean = loadedRom != null && NativeBridge.nativeLoadState()

    fun hasState(): Boolean = loadedRom != null && NativeBridge.nativeHasState()

    /** Internal cartridge title; often empty in homebrew. */
    fun title(): String = if (loadedRom == null) "" else NativeBridge.nativeGetTitle().trim()

    /** Game screen size in pixels: 240x160 for GBA, 160x144 for GB/GBC. */
    fun videoSize(): Pair<Int, Int> {
        val size = NativeBridge.nativeGetVideoSize()
        val width = size.getOrElse(0) { 0 }
        val height = size.getOrElse(1) { 0 }
        return if (width > 0 && height > 0) width to height else 240 to 160
    }

    /** CRC32 of the ROM (8 hex digits): key for the game's saves and cheats. */
    fun gameId(): String = if (loadedRom == null) "" else NativeBridge.nativeGetGameId()

    /** 1 = normal speed; 2 keeps the sound sped up; 4 and 8 are muted. */
    fun setSpeed(multiplier: Int) {
        if (loadedRom != null) NativeBridge.nativeSetSpeed(multiplier)
    }

    /**
     * Replaces the active cheats. Each entry is one cheat (lines separated by
     * '\n'). Returns the index of the first rejected one, or -1.
     */
    fun applyCheats(codes: List<String>): Int =
        if (loadedRom == null) -1 else NativeBridge.nativeCheatsApply(codes.toTypedArray())

    fun isValidCheat(code: String): Boolean = loadedRom != null && NativeBridge.nativeCheatValidate(code)

    /**
     * Replaces the in-game save with one from another emulator (.sav, .srm,
     * .gsv, .sps) and restarts the game. The previous save is kept as a backup.
     */
    fun importSave(data: ByteArray): Result =
        if (loadedRom == null) Result.FAILED else toResult(NativeBridge.nativeImportSave(data))

    /** The in-game save as a raw .sav (works in other emulators), or null. */
    fun exportSave(): ByteArray? = if (loadedRom == null) null else NativeBridge.nativeExportSave()

    fun hasSaveBackup(): Boolean = loadedRom != null && NativeBridge.nativeHasSaveBackup()

    /** Swaps the current save with the backup; doing it twice undoes it. */
    fun restoreSaveBackup(): Boolean = loadedRom != null && NativeBridge.nativeRestoreSaveBackup()

    private fun toResult(code: Int): Result = when (code) {
        0 -> Result.OK
        -1 -> Result.NOT_FOUND
        -2 -> Result.NOT_A_GAME
        -4 -> Result.BAD_SAVE
        else -> Result.FAILED
    }
}
