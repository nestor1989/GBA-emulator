// JNI entry points for com.nestordelrio.emu.core.NativeBridge (a Kotlin object,
// so every function receives the singleton instance as `thiz`).
#include <jni.h>

#include <android/log.h>

#include <stdlib.h>

#include "audio_out.h"
#include "emu_core.h"
#include "video_gles.h"

#define JNI_FN(name) Java_com_nestordelrio_emu_core_NativeBridge_##name

static void log_to_logcat(int level, const char* message) {
	int priority = ANDROID_LOG_DEBUG;
	switch (level) {
	case EMU_LOG_ERROR:
		priority = ANDROID_LOG_ERROR;
		break;
	case EMU_LOG_WARN:
		priority = ANDROID_LOG_WARN;
		break;
	case EMU_LOG_INFO:
		priority = ANDROID_LOG_INFO;
		break;
	default:
		break;
	}
	__android_log_write(priority, "emucore", message);
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
	(void) vm;
	(void) reserved;
	emu_set_log_fn(log_to_logcat);
	return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL JNI_FN(nativeProbe)(JNIEnv* env, jobject thiz, jstring path) {
	(void) thiz;
	const char* cPath = (*env)->GetStringUTFChars(env, path, NULL);
	if (!cPath) {
		return EMU_ERR_NOT_FOUND;
	}
	int result = emu_probe(cPath);
	(*env)->ReleaseStringUTFChars(env, path, cPath);
	return result;
}

JNIEXPORT jint JNICALL JNI_FN(nativeLoad)(JNIEnv* env, jobject thiz, jstring romPath, jstring saveDir, jstring stateDir) {
	(void) thiz;
	const char* cRom = (*env)->GetStringUTFChars(env, romPath, NULL);
	const char* cSave = (*env)->GetStringUTFChars(env, saveDir, NULL);
	const char* cState = (*env)->GetStringUTFChars(env, stateDir, NULL);
	int result = EMU_ERR_NOT_FOUND;
	if (cRom && cSave && cState) {
		result = emu_load(cRom, cSave, cState);
	}
	if (cRom) {
		(*env)->ReleaseStringUTFChars(env, romPath, cRom);
	}
	if (cSave) {
		(*env)->ReleaseStringUTFChars(env, saveDir, cSave);
	}
	if (cState) {
		(*env)->ReleaseStringUTFChars(env, stateDir, cState);
	}
	return result;
}

JNIEXPORT void JNICALL JNI_FN(nativeUnload)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	emu_unload();
}

JNIEXPORT void JNICALL JNI_FN(nativeStart)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	emu_start();
}

JNIEXPORT void JNICALL JNI_FN(nativeSetPaused)(JNIEnv* env, jobject thiz, jboolean paused) {
	(void) env;
	(void) thiz;
	emu_set_paused(paused == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetKeys)(JNIEnv* env, jobject thiz, jint keys) {
	(void) env;
	(void) thiz;
	emu_set_keys((uint32_t) keys);
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeSaveState)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return emu_save_state() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeLoadState)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return emu_load_state() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeHasState)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return emu_has_state() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(nativeFlushSave)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	emu_flush_save();
}

JNIEXPORT jstring JNICALL JNI_FN(nativeGetTitle)(JNIEnv* env, jobject thiz) {
	(void) thiz;
	char title[64];
	emu_get_title(title, sizeof(title));
	// Cartridge titles are ASCII; replace anything else so NewStringUTF never
	// receives invalid modified UTF-8.
	for (char* p = title; *p; ++p) {
		if ((unsigned char) *p < 0x20 || (unsigned char) *p > 0x7E) {
			*p = ' ';
		}
	}
	return (*env)->NewStringUTF(env, title);
}

JNIEXPORT jintArray JNICALL JNI_FN(nativeGetVideoSize)(JNIEnv* env, jobject thiz) {
	(void) thiz;
	unsigned width = 0;
	unsigned height = 0;
	emu_get_video_size(&width, &height);
	jint values[2] = { (jint) width, (jint) height };
	jintArray array = (*env)->NewIntArray(env, 2);
	if (array) {
		(*env)->SetIntArrayRegion(env, array, 0, 2, values);
	}
	return array;
}

JNIEXPORT jstring JNICALL JNI_FN(nativeGetGameId)(JNIEnv* env, jobject thiz) {
	(void) thiz;
	char id[16];
	emu_get_game_id(id, sizeof(id));
	return (*env)->NewStringUTF(env, id);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetSpeed)(JNIEnv* env, jobject thiz, jint speed) {
	(void) env;
	(void) thiz;
	emu_set_speed(speed > 0 ? (unsigned) speed : 1u);
}

JNIEXPORT jint JNICALL JNI_FN(nativeCheatsApply)(JNIEnv* env, jobject thiz, jobjectArray codes) {
	(void) thiz;
	jsize count = codes ? (*env)->GetArrayLength(env, codes) : 0;
	if (count <= 0) {
		return emu_cheats_apply(NULL, 0);
	}
	const char** strings = calloc((size_t) count, sizeof(char*));
	jstring* refs = calloc((size_t) count, sizeof(jstring));
	if (!strings || !refs) {
		free(strings);
		free(refs);
		return 0;
	}
	for (jsize i = 0; i < count; ++i) {
		refs[i] = (jstring) (*env)->GetObjectArrayElement(env, codes, i);
		strings[i] = refs[i] ? (*env)->GetStringUTFChars(env, refs[i], NULL) : NULL;
	}
	int result = emu_cheats_apply(strings, (size_t) count);
	for (jsize i = 0; i < count; ++i) {
		if (refs[i]) {
			if (strings[i]) {
				(*env)->ReleaseStringUTFChars(env, refs[i], strings[i]);
			}
			(*env)->DeleteLocalRef(env, refs[i]);
		}
	}
	free(strings);
	free(refs);
	return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeCheatValidate)(JNIEnv* env, jobject thiz, jstring code) {
	(void) thiz;
	const char* cCode = (*env)->GetStringUTFChars(env, code, NULL);
	if (!cCode) {
		return JNI_FALSE;
	}
	bool ok = emu_cheat_validate(cCode);
	(*env)->ReleaseStringUTFChars(env, code, cCode);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL JNI_FN(nativeImportSave)(JNIEnv* env, jobject thiz, jbyteArray data) {
	(void) thiz;
	jsize length = data ? (*env)->GetArrayLength(env, data) : 0;
	if (length <= 0) {
		return EMU_ERR_BAD_SAVE;
	}
	jbyte* bytes = (*env)->GetByteArrayElements(env, data, NULL);
	if (!bytes) {
		return EMU_ERR_BAD_SAVE;
	}
	int result = emu_import_save((const uint8_t*) bytes, (size_t) length);
	(*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
	return result;
}

JNIEXPORT jbyteArray JNICALL JNI_FN(nativeExportSave)(JNIEnv* env, jobject thiz) {
	(void) thiz;
	size_t size = 0;
	uint8_t* data = emu_export_save(&size);
	if (!data || !size) {
		free(data);
		return NULL;
	}
	jbyteArray array = (*env)->NewByteArray(env, (jsize) size);
	if (array) {
		(*env)->SetByteArrayRegion(env, array, 0, (jsize) size, (const jbyte*) data);
	}
	free(data);
	return array;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeHasSaveBackup)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return emu_has_save_backup() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeRestoreSaveBackup)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return emu_restore_save_backup() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeAudioStart)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	return audio_out_start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(nativeAudioStop)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	audio_out_stop();
}

JNIEXPORT void JNICALL JNI_FN(nativeGlInit)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	video_gl_init();
}

JNIEXPORT void JNICALL JNI_FN(nativeGlResize)(JNIEnv* env, jobject thiz, jint width, jint height) {
	(void) env;
	(void) thiz;
	video_gl_resize(width, height);
}

JNIEXPORT void JNICALL JNI_FN(nativeGlDraw)(JNIEnv* env, jobject thiz) {
	(void) env;
	(void) thiz;
	video_gl_draw();
}
