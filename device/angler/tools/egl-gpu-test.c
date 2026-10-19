/* SPDX-License-Identifier: MIT */
/*
 * Offscreen EGL/GBM probe for the mainline msm render node: a surfaceless
 * GLES2 context rendering into an FBO (Mesa's GBM platform has no pbuffer
 * configs). Clears, then draws a full-screen triangle with a shader, and
 * reads both results back.
 *
 * Build on the phone: cc -O2 -o egl-gpu-test egl-gpu-test.c -lEGL -lGLESv2 -lgbm
 * Usage: egl-gpu-test [device], default /dev/dri/renderD128. On simpledrm's
 * card with MESA_LOADER_DRIVER_OVERRIDE=kmsro it tests freedreno through
 * kmsro.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define W 64
#define H 64

static const char *vs_src =
	"attribute vec2 pos;\n"
	"void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src =
	"precision mediump float;\n"
	"void main() { gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }\n";

static int near(unsigned char v, int want)
{
	return abs((int)v - want) <= 8;
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader compile failed: %s\n", log);
		exit(1);
	}
	return s;
}

static void readback(const char *what, unsigned char px[4])
{
	glFinish();
	glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%s pixel=%u,%u,%u,%u err=0x%x\n", what, px[0], px[1], px[2],
	       px[3], glGetError());
}

int main(int argc, char **argv)
{
	static const GLfloat tri[] = { -1, -1, 3, -1, -1, 3 };
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
	EGLint cfg_attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	const char *renderer, *version;
	unsigned char clear_px[4], draw_px[4];
	struct gbm_device *gbm;
	EGLint major, minor, n = 0;
	GLuint fbo, rb, prog;
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig cfg;
	int fd, pass;

	const char *path = argc > 1 ? argv[1] : "/dev/dri/renderD128";

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	gbm = gbm_create_device(fd);
	if (!gbm) {
		fprintf(stderr, "gbm_create_device failed\n");
		return 1;
	}

	get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (!get_platform_display) {
		fprintf(stderr, "no eglGetPlatformDisplayEXT\n");
		return 1;
	}
	dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
		fprintf(stderr, "eglInitialize failed 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d vendor=%s\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR));
	if (!eglBindAPI(EGL_OPENGL_ES_API) ||
	    !eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &n) || n < 1) {
		fprintf(stderr, "eglChooseConfig failed 0x%x n=%d\n",
			eglGetError(), n);
		return 1;
	}
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
	if (ctx == EGL_NO_CONTEXT ||
	    !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "context failed 0x%x\n", eglGetError());
		return 1;
	}

	renderer = (const char *)glGetString(GL_RENDERER);
	version = (const char *)glGetString(GL_VERSION);
	printf("GL_RENDERER=%s\nGL_VERSION=%s\n",
	       renderer ? renderer : "(null)", version ? version : "(null)");

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, W, H);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				  GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "FBO incomplete\n");
		return 1;
	}
	glViewport(0, 0, W, H);

	glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	readback("clear", clear_px);

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	readback("draw", draw_px);

	/* RGB565 storage: compare loosely */
	pass = near(clear_px[0], 64) && near(clear_px[1], 128) &&
	       near(clear_px[2], 191) && near(draw_px[0], 255) &&
	       near(draw_px[1], 128) && near(draw_px[2], 0);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	gbm_device_destroy(gbm);
	close(fd);

	printf("%s\n", pass ? "PASS: clear and draw on the render node" :
			      "FAIL: unexpected pixels");
	return !pass;
}
