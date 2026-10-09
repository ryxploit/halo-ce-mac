/*
HOST_GL.C

OpenGL for the guest. The guest's renderer is the OpenGL ES 3 one of the
Android port (port/linux/src/d3d8_gl.c with HALO_GLES); macOS has no OpenGL
ES, so it runs on the system's OpenGL 4.1 core profile, of which ES 3.0 is
nearly a subset: 101 of the ES path's 103 entry points are there
(docs/macos-port-audit.md). What differs is made up here:

- the context is 4.1 core whatever the guest asks for (host_sdl.c);
- the version is reported as ES 3.0, so the renderer keeps to ES 3.0's
  features, and its shaders, written for GLSL ES 3.00, are given GLSL 4.10's
  #version line (host_gl_shader_source);
- what ES 3 always does and desktop OpenGL only when asked is turned on:
  gl_PointSize, seamless cube maps;
- border clamping, core in desktop OpenGL, is reported as its ES extension;
- glInvalidateFramebuffer (ES 3.0, desktop 4.3) is a hint, and is dropped.

The other entry points go straight to the driver (tools/macos_host_bridges.py).
*/

#include "macos_host.h"

#include <SDL3/SDL.h>
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_FENCE_SLOTS 8

static GLsync frame_fences[FRAME_FENCE_SLOTS];
static char shading_version[64];

void host_gl_prepare_context(void)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
}

void host_gl_context_created(void)
{
	glEnable(GL_PROGRAM_POINT_SIZE);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	snprintf(shading_version, sizeof(shading_version), "%s", (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
	host_logf(HOST_LOG_INFO, "OpenGL %s (%s, %s), GLSL %s, presented to the game as OpenGL ES 3.0",
		(const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER),
		(const char *)glGetString(GL_VENDOR), shading_version);
}

/* ---------- strings, extensions and the version */

static int desktop_has_extension(const char *name)
{
	GLint count = 0, index;

	glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	for (index = 0; index < count; index++)
	{
		const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)index);

		if (extension && !strcmp(extension, name))
			return 1;
	}
	return 0;
}

int host_gl_has_extension(const char *name)
{
	/* core in desktop OpenGL since 1.3, with ES's enumerants */
	if (!strcmp(name, "GL_EXT_texture_border_clamp") || !strcmp(name, "GL_OES_texture_border_clamp"))
		return 1;
	return desktop_has_extension(name);
}

void host_gl_get_string(uint32_t name, int index, char *buffer, uint32_t size)
{
	const char *text;

	if (!size)
		return;
	if (index < 0 && name == GL_VERSION)
		text = "OpenGL ES 3.0 (macOS OpenGL 4.1 core)";
	else if (index < 0 && name == GL_SHADING_LANGUAGE_VERSION)
		text = "OpenGL ES GLSL ES 3.00";
	else
		text = (const char *)(index >= 0 ? glGetStringi(name, (GLuint)index) : glGetString(name));
	SDL_strlcpy(buffer, text ? text : "", size);
}

/* glGetIntegerv: the version is ES 3.0's */
void host_gl_get_integerv(GLenum name, GLint *value)
{
	if (name == GL_MAJOR_VERSION)
	{
		*value = 3;
		return;
	}
	if (name == GL_MINOR_VERSION)
	{
		*value = 0;
		return;
	}
	glGetIntegerv(name, value);
}

/* ---------- shaders */

/* glShaderSource with the guest's strings (an array of 64-bit guest
pointers, which the guest's entry point widened, android_gl_stubs.py): a
GLSL ES "#version 300 es" (or 310 es) line becomes GLSL 4.10's */
void host_gl_shader_source(GLuint shader, GLsizei count, const uint64_t *strings, const GLint *lengths)
{
	const GLchar *sources[16];
	GLint source_lengths[16];
	char *rewritten = NULL;
	GLsizei index;

	if (count < 0 || count > 16)
	{
		host_logf(HOST_LOG_ERROR, "glShaderSource with %d strings", (int)count);
		return;
	}
	for (index = 0; index < count; index++)
	{
		sources[index] = host_pointer(strings[index]);
		source_lengths[index] = lengths ? lengths[index] : -1;
	}
	if (count > 0)
	{
		const char *first = sources[0];
		size_t length = source_lengths[0] >= 0 ? (size_t)source_lengths[0] : strlen(first);
		const char *line_end = memchr(first, '\n', length);
		size_t line_length = line_end ? (size_t)(line_end - first) : length;

		if (line_length >= 9 && !strncmp(first, "#version ", 9) && line_length < 64 &&
			memmem(first, line_length, " es", 3))
		{
			static const char replacement[] = "#version 410 core";
			size_t rest = length - line_length;

			rewritten = malloc(sizeof(replacement) + rest);
			if (rewritten)
			{
				memcpy(rewritten, replacement, sizeof(replacement) - 1);
				memcpy(rewritten + sizeof(replacement) - 1, first + line_length, rest);
				sources[0] = rewritten;
				source_lengths[0] = (GLint)(sizeof(replacement) - 1 + rest);
			}
		}
	}
	glShaderSource(shader, count, sources, source_lengths);
	free(rewritten);
}

void host_gl_invalidate_framebuffer(GLenum target, GLsizei count, const GLenum *attachments)
{
	/* a hint the driver may ignore; desktop OpenGL has it from 4.3 */
	(void)target;
	(void)count;
	(void)attachments;
}

/* ---------- the guest's own OpenGL services (port/linux/src/xgpu.h) */

/* one 32-bit word of a buffer object (the visibility test counters of
d3d8_gl.c, which it asks for only with atomic counters, an ES 3.1 feature
this context is not reported to have) */
uint32_t host_gl_read_buffer_word(uint32_t buffer, uint32_t offset)
{
	uint32_t value = 0;
	GLint previous = 0;

	glGetIntegerv(GL_COPY_READ_BUFFER, &previous);
	glBindBuffer(GL_COPY_READ_BUFFER, buffer);
	glGetBufferSubData(GL_COPY_READ_BUFFER, offset, sizeof(value), &value);
	glBindBuffer(GL_COPY_READ_BUFFER, (GLuint)previous);
	return value;
}

/* (as port/android/host/host_gl.c: a fence per frame of the ring of stream
buffers, and unsynchronized writes into ranges no queued draw reads) */
void host_gl_fence_frame(uint32_t slot)
{
	if (slot >= FRAME_FENCE_SLOTS)
		return;
	if (frame_fences[slot])
		glDeleteSync(frame_fences[slot]);
	frame_fences[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void host_gl_wait_frame(uint32_t slot)
{
	if (slot >= FRAME_FENCE_SLOTS || !frame_fences[slot])
		return;
	/* at most a second: a lost context must not hang the game */
	glClientWaitSync(frame_fences[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
	glDeleteSync(frame_fences[slot]);
	frame_fences[slot] = NULL;
}

void host_gl_buffer_write(uint32_t target, uint32_t offset, uint32_t size, const void *data)
{
	void *mapping = glMapBufferRange(target, offset, size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);

	if (!mapping)
	{
		glBufferSubData(target, offset, size, data);
		return;
	}
	memcpy(mapping, data, size);
	glUnmapBuffer(target);
}
