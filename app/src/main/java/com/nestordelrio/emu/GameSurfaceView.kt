package com.nestordelrio.emu

import android.content.Context
import android.opengl.GLSurfaceView
import com.nestordelrio.emu.core.NativeBridge
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** Shows the emulator output. Drawing happens in native code on the GL thread. */
class GameSurfaceView(context: Context) : GLSurfaceView(context) {

    init {
        setEGLContextClientVersion(2)
        preserveEGLContextOnPause = true
        setRenderer(Renderer())
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    private class Renderer : GLSurfaceView.Renderer {
        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) = NativeBridge.nativeGlInit()

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) =
            NativeBridge.nativeGlResize(width, height)

        override fun onDrawFrame(gl: GL10?) = NativeBridge.nativeGlDraw()
    }
}
