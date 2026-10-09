/*
GUEST_SDL_DESKTOP.C

The SDL3 functions the desktop platform layer calls (sdl_platform.c,
port_config.c, menu_files.c) beyond those of the Android guest
(port/android/guest/runtime/guest_sdl.c), for the macOS guest. Windows,
displays, the 2D renderer and the dialogs are the host's (port/macos/host),
reached through the imports of macos_guest_host.h; time, atomics and files
are done here, over the guest's C library.
*/

/* before SDL: musl's alloca.h must come first */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "guest_host.h"
#include "macos_guest_host.h"
#include "posix.h"

/* ---------- time and atomics */

Uint64 SDL_GetTicksNS(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (Uint64)now.tv_sec * 1000000000ull + (Uint64)now.tv_nsec;
}

void SDL_DelayPrecise(Uint64 nanoseconds)
{
	struct timespec duration;

	duration.tv_sec = (time_t)(nanoseconds / 1000000000ull);
	duration.tv_nsec = (long)(nanoseconds % 1000000000ull);
	while (nanosleep(&duration, &duration) != 0)
	{
	}
}

int SDL_GetAtomicInt(SDL_AtomicInt *atomic)
{
	return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
}

int SDL_SetAtomicInt(SDL_AtomicInt *atomic, int value)
{
	return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
}

/* ---------- files

The app bundle is read-only, so where the desktop ports look in their
executable's folder (config.toml, port_config.c), the macOS guest looks in
the user's folder, ~/Library/Application Support/Halo CE, which the host
names in HALO_MACOS_USER_ROOT (port/macos/host). */

const char *SDL_GetBasePath(void)
{
	static char path[1024];

	if (!path[0])
	{
		const char *root = getenv("HALO_MACOS_USER_ROOT");

		snprintf(path, sizeof(path), "%s/", root && *root ? root : ".");
	}
	return path;
}

void *SDL_LoadFile(const char *file, size_t *size)
{
	FILE *stream = fopen(file, "rb");
	char *data = NULL;
	long length;

	if (!stream)
		return NULL;
	if (fseek(stream, 0, SEEK_END) == 0 && (length = ftell(stream)) >= 0 && fseek(stream, 0, SEEK_SET) == 0)
	{
		/* (SDL's adds a NUL after the data, as some callers expect) */
		data = malloc((size_t)length + 1);
		if (data && fread(data, 1, (size_t)length, stream) == (size_t)length)
		{
			data[length] = 0;
			if (size)
				*size = (size_t)length;
		}
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(stream);
	return data;
}

bool SDL_SaveFile(const char *file, const void *data, size_t size)
{
	FILE *stream = fopen(file, "wb");
	bool written;

	if (!stream)
		return false;
	written = fwrite(data, 1, size, stream) == size;
	return fclose(stream) == 0 && written;
}

/* whether name matches pattern, where * and ? never match a / */
static bool glob_match(const char *pattern, const char *name, bool case_insensitive)
{
	for (; *pattern; pattern++, name++)
	{
		if (*pattern == '*')
		{
			const char *rest;

			for (rest = name;; rest++)
			{
				if (glob_match(pattern + 1, rest, case_insensitive))
					return true;
				if (!*rest || *rest == '/')
					return false;
			}
		}
		if (!*name)
			return false;
		if (*pattern == '?')
		{
			if (*name == '/')
				return false;
			continue;
		}
		if (case_insensitive ? tolower((unsigned char)*pattern) != tolower((unsigned char)*name) :
			*pattern != *name)
		{
			return false;
		}
	}
	return !*name;
}

struct glob_list
{
	char **names;
	int count, capacity;
	size_t text_bytes;
};

static void glob_add(struct glob_list *list, const char *name)
{
	if (list->count == list->capacity)
	{
		int capacity = list->capacity ? list->capacity * 2 : 32;
		char **names = realloc(list->names, (size_t)capacity * sizeof(*names));

		if (!names)
			return;
		list->names = names;
		list->capacity = capacity;
	}
	list->names[list->count] = strdup(name);
	if (list->names[list->count])
	{
		list->text_bytes += strlen(name) + 1;
		list->count++;
	}
}

/* adds the entries under folder/relative, depth folders deep at most */
static void glob_walk(struct glob_list *list, const char *folder, const char *relative, int depth,
	const char *pattern, bool case_insensitive)
{
	char path[1024], name[256], entry[1024];
	void *directory;

	snprintf(path, sizeof(path), "%s%s%s", folder, *relative ? "/" : "", relative);
	directory = posix_directory_open(path);
	if (!directory)
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		struct posix_file_information information;
		char full[1024];

		snprintf(entry, sizeof(entry), "%s%s%s", relative, *relative ? "/" : "", name);
		if (!pattern || glob_match(pattern, entry, case_insensitive))
			glob_add(list, entry);
		snprintf(full, sizeof(full), "%s/%s", folder, entry);
		if (depth > 0 && posix_stat(full, &information) == 0 && (information.flags & _posix_file_is_directory))
			glob_walk(list, folder, entry, depth - 1, pattern, case_insensitive);
	}
	posix_directory_close(directory);
}

/* SDL's: the matching entries' paths relative to path, in one allocation */
char **SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count)
{
	struct glob_list list = {0};
	int depth = 0, index;
	const char *character;
	char **result, *text;

	/* as deep as the pattern names folders (everything, without one) */
	if (pattern)
	{
		for (character = pattern; *character; character++)
			depth += *character == '/';
	}
	else
	{
		depth = 64;
	}
	glob_walk(&list, path, "", depth, pattern, (flags & SDL_GLOB_CASEINSENSITIVE) != 0);
	result = malloc((size_t)(list.count + 1) * sizeof(*result) + list.text_bytes);
	if (result)
	{
		text = (char *)(result + list.count + 1);
		for (index = 0; index < list.count; index++)
		{
			size_t length = strlen(list.names[index]) + 1;

			memcpy(text, list.names[index], length);
			result[index] = text;
			text += length;
		}
		result[list.count] = NULL;
		if (count)
			*count = list.count;
	}
	for (index = 0; index < list.count; index++)
		free(list.names[index]);
	free(list.names);
	return result;
}

/* ---------- windows and displays */

void SDL_DestroyWindow(SDL_Window *window)
{
	host_sdl_destroy_window((unsigned int)window);
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
	return (SDL_WindowFlags)host_sdl_window_flags((unsigned int)window);
}

bool SDL_GetWindowSize(SDL_Window *window, int *width, int *height)
{
	int w = 0, h = 0;

	host_sdl_window_size((unsigned int)window, &w, &h);
	if (width)
		*width = w;
	if (height)
		*height = h;
	return true;
}

bool SDL_SetWindowSize(SDL_Window *window, int width, int height)
{
	return host_sdl_set_window_size((unsigned int)window, width, height) != 0;
}

bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
	return host_sdl_set_window_fullscreen((unsigned int)window, fullscreen) != 0;
}

static void display_mode_from_host(SDL_DisplayMode *mode, const struct macos_display_mode *host)
{
	memset(mode, 0, sizeof(*mode));
	mode->displayID = host->display;
	mode->format = (SDL_PixelFormat)host->format;
	mode->w = host->width;
	mode->h = host->height;
	mode->pixel_density = host->pixel_density;
	mode->refresh_rate = host->refresh_rate;
	mode->refresh_rate_numerator = host->refresh_rate_numerator;
	mode->refresh_rate_denominator = host->refresh_rate_denominator;
}

static void display_mode_to_host(struct macos_display_mode *host, const SDL_DisplayMode *mode)
{
	host->display = mode->displayID;
	host->format = (unsigned int)mode->format;
	host->width = mode->w;
	host->height = mode->h;
	host->pixel_density = mode->pixel_density;
	host->refresh_rate = mode->refresh_rate;
	host->refresh_rate_numerator = mode->refresh_rate_numerator;
	host->refresh_rate_denominator = mode->refresh_rate_denominator;
}

bool SDL_SetWindowFullscreenMode(SDL_Window *window, const SDL_DisplayMode *mode)
{
	struct macos_display_mode host = {0};

	if (mode)
		display_mode_to_host(&host, mode);
	return host_sdl_set_window_fullscreen_mode((unsigned int)window, mode != NULL, &host) != 0;
}

void SDL_WarpMouseInWindow(SDL_Window *window, float x, float y)
{
	host_sdl_warp_mouse_in_window((unsigned int)window, x, y);
}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *window)
{
	return (SDL_DisplayID)host_sdl_display_for_window((unsigned int)window);
}

SDL_DisplayID SDL_GetPrimaryDisplay(void)
{
	return (SDL_DisplayID)host_sdl_primary_display();
}

/* SDL returns modes it keeps until the displays change; here a ring of
copies, each valid until a few more modes have been asked for (the platform
layer reads a mode right after asking for it) */
static const SDL_DisplayMode *display_mode(SDL_DisplayID display, int which)
{
	static __thread SDL_DisplayMode modes[8];
	static __thread unsigned int next;
	struct macos_display_mode host;
	SDL_DisplayMode *mode;

	if (!host_sdl_display_mode(display, which, &host))
		return NULL;
	mode = &modes[next++ % 8];
	display_mode_from_host(mode, &host);
	return mode;
}

const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID display)
{
	return display_mode(display, 0);
}

const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID display)
{
	return display_mode(display, 1);
}

SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID display, int *count)
{
	struct macos_display_mode host[64];
	int found = host_sdl_fullscreen_display_modes(display, host, 64), index;
	SDL_DisplayMode **result;
	SDL_DisplayMode *modes;

	if (found < 0)
		found = 0;
	/* SDL's single allocation: the pointers, then the modes */
	result = malloc((size_t)(found + 1) * sizeof(*result) + (size_t)found * sizeof(*modes));
	if (!result)
		return NULL;
	modes = (SDL_DisplayMode *)(result + found + 1);
	for (index = 0; index < found; index++)
	{
		display_mode_from_host(&modes[index], &host[index]);
		result[index] = &modes[index];
	}
	result[found] = NULL;
	if (count)
		*count = found;
	return result;
}

bool SDL_GetClosestFullscreenDisplayMode(SDL_DisplayID display, int w, int h, float refresh_rate,
	bool include_high_density_modes, SDL_DisplayMode *closest)
{
	struct macos_display_mode host;

	if (!host_sdl_closest_fullscreen_display_mode(display, w, h, refresh_rate, include_high_density_modes, &host))
		return false;
	display_mode_from_host(closest, &host);
	return true;
}

bool SDL_GetDisplayUsableBounds(SDL_DisplayID display, SDL_Rect *rect)
{
	int rectangle[4];

	if (!host_sdl_display_usable_bounds(display, rectangle))
		return false;
	rect->x = rectangle[0];
	rect->y = rectangle[1];
	rect->w = rectangle[2];
	rect->h = rectangle[3];
	return true;
}

/* ---------- events */

void SDL_PumpEvents(void)
{
	host_sdl_pump_events();
}

bool SDL_PushEvent(SDL_Event *event)
{
	return host_sdl_push_event(event) != 0;
}

/* ---------- the 2D renderer */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name)
{
	(void)name;
	return (SDL_Renderer *)host_sdl_create_renderer((unsigned int)window);
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	host_sdl_destroy_renderer((unsigned int)renderer);
}

bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync)
{
	return host_sdl_set_render_vsync((unsigned int)renderer, vsync) != 0;
}

bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	return host_sdl_set_render_draw_color((unsigned int)renderer, r, g, b, a) != 0;
}

bool SDL_RenderClear(SDL_Renderer *renderer)
{
	return host_sdl_render_clear((unsigned int)renderer) != 0;
}

bool SDL_SetRenderScale(SDL_Renderer *renderer, float scale_x, float scale_y)
{
	return host_sdl_set_render_scale((unsigned int)renderer, scale_x, scale_y) != 0;
}

bool SDL_RenderDebugText(SDL_Renderer *renderer, float x, float y, const char *text)
{
	return host_sdl_render_debug_text((unsigned int)renderer, x, y, text) != 0;
}

bool SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rect)
{
	if (!rect)
		return host_sdl_render_fill_rect((unsigned int)renderer, 0.0f, 0.0f, -1.0f, -1.0f) != 0;
	return host_sdl_render_fill_rect((unsigned int)renderer, rect->x, rect->y, rect->w, rect->h) != 0;
}

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
	return host_sdl_render_present((unsigned int)renderer) != 0;
}

/* ---------- dialogs */

/* appends text and its NUL to buffer at *used */
static void append_separated(char *buffer, size_t size, size_t *used, const char *text)
{
	size_t length = strlen(text ? text : "") + 1;

	if (*used + length > size)
		return;
	memcpy(buffer + *used, text ? text : "", length);
	*used += length;
}

bool SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
	int ids[8];
	unsigned int flags[8];
	char texts[512];
	size_t used = 0;
	int count = data->numbuttons < 8 ? data->numbuttons : 8, index, answer = -1;

	for (index = 0; index < count; index++)
	{
		ids[index] = data->buttons[index].buttonID;
		flags[index] = (unsigned int)data->buttons[index].flags;
		append_separated(texts, sizeof(texts), &used, data->buttons[index].text);
	}
	if (!host_sdl_show_message_box((unsigned int)data->flags, data->title ? data->title : "",
			data->message ? data->message : "", count, ids, flags, texts, &answer))
	{
		return false;
	}
	if (buttonid)
		*buttonid = answer;
	return true;
}

/* the host's dialog waits for the answer, so the callback comes before this
returns, on this thread (SDL lets it come on any thread, at any time) */
void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
	const SDL_DialogFileFilter *filters, int nfilters, const char *default_location, bool allow_many)
{
	char names[512], patterns[512], path[1024];
	size_t names_used = 0, patterns_used = 0;
	const char *files[2] = { NULL, NULL };
	int index;

	(void)window;
	(void)default_location;
	(void)allow_many;
	for (index = 0; filters && index < nfilters; index++)
	{
		append_separated(names, sizeof(names), &names_used, filters[index].name);
		append_separated(patterns, sizeof(patterns), &patterns_used, filters[index].pattern);
	}
	if (host_sdl_choose_open_file(names, patterns, filters ? nfilters : 0, path, sizeof(path)))
		files[0] = path;
	callback(userdata, files, -1);
}
