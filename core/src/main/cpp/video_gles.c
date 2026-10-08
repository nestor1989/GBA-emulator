// Draws the latest emulator frame with OpenGL ES 2.0, as large as the view
// allows (aspect ratio kept, centered).
//
// Scaling uses "sharp bilinear": each game pixel is drawn as a solid block and
// only the one-screen-pixel seam between blocks is blended. At non-integer
// sizes (e.g. 4.5x on a 1080 px wide phone) that avoids both the uneven pixel
// widths of nearest-neighbor and the blur of plain bilinear filtering.
#include "video_gles.h"

#include "emu_core.h"

#include <GLES2/gl2.h>
#include <android/log.h>
#include <math.h>
#include <stdbool.h>

#define TEX_SIZE 256 // power of two, fits every core (max 256x224)

static GLuint g_program;
static GLuint g_texture;
static GLint g_attrPos = -1;
static GLint g_attrUv = -1;
static GLint g_uniTex = -1;
static GLint g_uniTexSize = -1;
static GLint g_uniImageSize = -1;
static GLint g_uniPrescale = -1;
static int g_viewW;
static int g_viewH;
static bool g_forceUpload;

static const char* VERTEX_SHADER =
	"attribute vec2 aPos;\n"
	"attribute vec2 aUv;\n"
	"varying vec2 vUv;\n"
	"void main() {\n"
	"  vUv = aUv;\n"
	"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"}\n";

// vUv is in texture pixels. Inside a game pixel the sample snaps to its
// center (solid color); within 0.5/prescale of an edge it slides linearly to
// the neighbor's center, which the GPU's bilinear filter blends.
static const char* FRAGMENT_SHADER =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
	"varying vec2 vUv;\n"
	"uniform sampler2D uTex;\n"
	"uniform vec2 uTexSize;\n"
	"uniform vec2 uImageSize;\n"
	"uniform vec2 uPrescale;\n"
	"void main() {\n"
	"  vec2 pixel = floor(vUv);\n"
	"  vec2 offset = vUv - pixel - 0.5;\n"
	"  vec2 band = 0.5 - 0.5 / uPrescale;\n"
	"  vec2 shifted = (offset - clamp(offset, -band, band)) * uPrescale + 0.5;\n"
	"  vec2 coord = clamp(pixel + shifted, vec2(0.5), uImageSize - 0.5);\n"
	"  gl_FragColor = vec4(texture2D(uTex, coord / uTexSize).rgb, 1.0);\n"
	"}\n";

static GLuint compile(GLenum type, const char* source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		__android_log_print(ANDROID_LOG_ERROR, "emucore", "shader: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

void video_gl_init(void) {
	// Called on every new EGL context: old GL objects are already gone.
	GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SHADER);
	GLuint fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
	g_program = glCreateProgram();
	glAttachShader(g_program, vs);
	glAttachShader(g_program, fs);
	glLinkProgram(g_program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint linked = GL_FALSE;
	glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
	if (!linked) {
		__android_log_print(ANDROID_LOG_ERROR, "emucore", "program link failed");
	}
	g_attrPos = glGetAttribLocation(g_program, "aPos");
	g_attrUv = glGetAttribLocation(g_program, "aUv");
	g_uniTex = glGetUniformLocation(g_program, "uTex");
	g_uniTexSize = glGetUniformLocation(g_program, "uTexSize");
	g_uniImageSize = glGetUniformLocation(g_program, "uImageSize");
	g_uniPrescale = glGetUniformLocation(g_program, "uPrescale");

	glGenTextures(1, &g_texture);
	glBindTexture(GL_TEXTURE_2D, g_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	g_forceUpload = true;
}

void video_gl_resize(int width, int height) {
	g_viewW = width;
	g_viewH = height;
}

void video_gl_draw(void) {
	glViewport(0, 0, g_viewW, g_viewH);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	unsigned width = 0;
	unsigned height = 0;
	unsigned stride = 0;
	bool fresh = false;
	const uint32_t* pixels = emu_video_latest(&width, &height, &stride, &fresh);
	if (!pixels || !g_program || g_viewW <= 0 || g_viewH <= 0 || stride > TEX_SIZE || height > TEX_SIZE) {
		return;
	}

	glBindTexture(GL_TEXTURE_2D, g_texture);
	if (fresh || g_forceUpload) {
		// Rows are `stride` pixels wide; GLES2 has no UNPACK_ROW_LENGTH, so
		// upload full rows and crop with the texture coordinates.
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei) stride, (GLsizei) height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		g_forceUpload = false;
	}

	float scale = fminf((float) g_viewW / (float) width, (float) g_viewH / (float) height);
	float prescale = fmaxf(floorf(scale), 1.f);
	int drawW = (int) ((float) width * scale);
	int drawH = (int) ((float) height * scale);
	int x = (g_viewW - drawW) / 2;
	int y = (g_viewH - drawH) / 2;
	glViewport(x, y, drawW, drawH);

	// Texture coordinates in pixels; row 0 is the top of the image.
	const float u = (float) width;
	const float v = (float) height;
	const GLfloat positions[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
	const GLfloat uvs[] = { 0.f, v, u, v, 0.f, 0.f, u, 0.f };

	glUseProgram(g_program);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(g_uniTex, 0);
	glUniform2f(g_uniTexSize, (float) TEX_SIZE, (float) TEX_SIZE);
	glUniform2f(g_uniImageSize, (float) width, (float) height);
	glUniform2f(g_uniPrescale, prescale, prescale);
	glVertexAttribPointer((GLuint) g_attrPos, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glEnableVertexAttribArray((GLuint) g_attrPos);
	glVertexAttribPointer((GLuint) g_attrUv, 2, GL_FLOAT, GL_FALSE, 0, uvs);
	glEnableVertexAttribArray((GLuint) g_attrUv);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
