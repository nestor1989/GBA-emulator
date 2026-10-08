package com.nestordelrio.emu.core

/**
 * Raw JNI surface of libemucore. Use [Emulator] instead of calling this directly.
 *
 * The functions must stay public: Kotlin mangles the JVM names of `internal`
 * members, and JNI resolves them by name.
 */
object NativeBridge {
    init {
        System.loadLibrary("emucore")
    }

    external fun nativeProbe(path: String): Int
    external fun nativeLoad(romPath: String, saveDir: String, stateDir: String): Int
    external fun nativeUnload()
    external fun nativeStart()
    external fun nativeSetPaused(paused: Boolean)
    external fun nativeSetKeys(keys: Int)
    external fun nativeSaveState(): Boolean
    external fun nativeLoadState(): Boolean
    external fun nativeHasState(): Boolean
    external fun nativeFlushSave()
    external fun nativeGetTitle(): String
    external fun nativeGetVideoSize(): IntArray
    external fun nativeGetGameId(): String

    external fun nativeSetSpeed(speed: Int)
    external fun nativeCheatsApply(codes: Array<String>): Int
    external fun nativeCheatValidate(code: String): Boolean
    external fun nativeImportSave(data: ByteArray): Int
    external fun nativeExportSave(): ByteArray?
    external fun nativeHasSaveBackup(): Boolean
    external fun nativeRestoreSaveBackup(): Boolean

    external fun nativeAudioStart(): Boolean
    external fun nativeAudioStop()

    external fun nativeGlInit()
    external fun nativeGlResize(width: Int, height: Int)
    external fun nativeGlDraw()
}
