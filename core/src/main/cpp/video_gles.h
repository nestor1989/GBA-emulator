#ifndef VIDEO_GLES_H
#define VIDEO_GLES_H

#ifdef __cplusplus
extern "C" {
#endif

// All three run on the GL thread (GLSurfaceView.Renderer callbacks).
void video_gl_init(void);
void video_gl_resize(int width, int height);
void video_gl_draw(void);

#ifdef __cplusplus
}
#endif

#endif
