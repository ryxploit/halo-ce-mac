/*
GUEST_GLES.C

What the desktop platform layer asks of gles_desktop.c (the in-process
OpenGL ES services of the Linux and Windows --gles builds), for the macOS
guest. There the host owns the context and serves the host_gl_* functions,
as Android's does: the context it makes is its choice, not the guest's
(port/macos/host), and OpenGL's debug output is the host's to turn on.
*/

/* port/linux/src/platform.h's, which needs the XDK's headers */
void platform_log(const char *format, ...);

/* sdl_platform.c, before the context is made */
void gles_desktop_choose_driver(void)
{
	platform_log("OpenGL ES: the macOS host's");
}

/* d3d8_gl.c, with debug.gl_debug */
void gles_desktop_debug_output(void)
{
}
