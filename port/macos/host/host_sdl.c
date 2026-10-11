/*
HOST_SDL.C

SDL3 on behalf of the guest: the Android guest's calls
(port/android/guest/runtime/guest_sdl.c, as port/android/host/host_sdl.c
serves them) and the desktop platform layer's
(port/macos/guest/guest_sdl_desktop.c). SDL objects are 64-bit pointers,
which the guest cannot hold; it gets small handles into the table here.

The game's main() runs on the main thread (host_thread.c), where SDL and
Cocoa need windows, events and dialogs to be. SDL's audio thread has no
stack in the arena, so the audio callback is handed to a thread that has one.
The guest's pointer arguments arrive here already translated
(tools/macos_host_bridges.py).
*/

#include "macos_host.h"
#include "macos_guest_host.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define HANDLE_COUNT 256

enum handle_type
{
	_handle_free,
	_handle_window,
	_handle_context,
	_handle_gamepad,
	_handle_audio,
	_handle_renderer,
};

struct handle
{
	int type;
	void *object;
};

static struct handle handles[HANDLE_COUNT];
static pthread_mutex_t handle_lock = PTHREAD_MUTEX_INITIALIZER;
/* a handle's context besides its object: an audio stream's binding */
static void *handle_contexts[HANDLE_COUNT];

static uint32_t handle_new(int type, void *object)
{
	uint32_t index;

	if (!object)
		return 0;
	pthread_mutex_lock(&handle_lock);
	/* an object that already has a handle keeps it */
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == type && handles[index].object == object)
		{
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == _handle_free)
		{
			handles[index].type = type;
			handles[index].object = object;
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	pthread_mutex_unlock(&handle_lock);
	host_logf(HOST_LOG_ERROR, "out of SDL handles");
	return 0;
}

static void *handle_get(uint32_t handle, int type)
{
	void *object = NULL;

	if (handle == 0 || handle >= HANDLE_COUNT)
		return NULL;
	pthread_mutex_lock(&handle_lock);
	if (handles[handle].type == type)
		object = handles[handle].object;
	pthread_mutex_unlock(&handle_lock);
	return object;
}

static void handle_release(uint32_t handle)
{
	if (handle == 0 || handle >= HANDLE_COUNT)
		return;
	pthread_mutex_lock(&handle_lock);
	handles[handle].type = _handle_free;
	handles[handle].object = NULL;
	handle_contexts[handle] = NULL;
	pthread_mutex_unlock(&handle_lock);
}

static void handle_context_set(uint32_t handle, void *context)
{
	if (handle == 0 || handle >= HANDLE_COUNT)
		return;
	pthread_mutex_lock(&handle_lock);
	handle_contexts[handle] = context;
	pthread_mutex_unlock(&handle_lock);
}

static void *handle_context_get(uint32_t handle)
{
	void *context = NULL;

	if (handle == 0 || handle >= HANDLE_COUNT)
		return NULL;
	pthread_mutex_lock(&handle_lock);
	context = handle_contexts[handle];
	pthread_mutex_unlock(&handle_lock);
	return context;
}

/* ---------- general */

int host_sdl_init(uint32_t flags)
{
	return SDL_Init((SDL_InitFlags)flags);
}

int host_sdl_set_hint(const char *name, const char *value)
{
	return SDL_SetHint(name, value);
}

void host_sdl_get_error(char *buffer, uint32_t size)
{
	SDL_strlcpy(buffer, SDL_GetError(), size);
}

/* a failure of the guest's own (a call the host does not make), which the
guest's SDL_GetError then returns */
void host_sdl_set_error(const char *text)
{
	SDL_SetError("%s", text);
}

void host_sdl_scancode_name(int32_t scancode, char *buffer, uint32_t size)
{
	SDL_strlcpy(buffer, SDL_GetScancodeName((SDL_Scancode)scancode), size);
}

int32_t host_sdl_scancode_from_name(const char *name)
{
	return (int32_t)SDL_GetScancodeFromName(name);
}

int64_t host_sdl_ticks(void)
{
	return (int64_t)SDL_GetTicks();
}

int64_t host_sdl_thread_id(void)
{
	return (int64_t)SDL_GetCurrentThreadID();
}

/* ---------- windows */

uint32_t host_sdl_create_window(const char *title, int width, int height, int64_t flags)
{
	SDL_Window *window = SDL_CreateWindow(title, width, height, (SDL_WindowFlags)flags);

	if (!window)
		host_logf(HOST_LOG_ERROR, "SDL_CreateWindow: %s", SDL_GetError());
	return handle_new(_handle_window, window);
}

void host_sdl_destroy_window(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);

	if (!object)
		return;
	handle_release(window);
	SDL_DestroyWindow(object);
}

void host_sdl_window_size_in_pixels(uint32_t window, int *width, int *height)
{
	SDL_Window *object = handle_get(window, _handle_window);

	*width = 0;
	*height = 0;
	if (object)
		SDL_GetWindowSizeInPixels(object, width, height);
}

void host_sdl_window_size(uint32_t window, int *width, int *height)
{
	SDL_Window *object = handle_get(window, _handle_window);

	*width = 0;
	*height = 0;
	if (object)
		SDL_GetWindowSize(object, width, height);
}

int host_sdl_set_window_size(uint32_t window, int width, int height)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? SDL_SetWindowSize(object, width, height) : 0;
}

int64_t host_sdl_window_flags(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? (int64_t)SDL_GetWindowFlags(object) : 0;
}

int host_sdl_set_window_fullscreen(uint32_t window, int fullscreen)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? SDL_SetWindowFullscreen(object, fullscreen != 0) : 0;
}

int host_sdl_set_relative_mouse(uint32_t window, int enabled)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? SDL_SetWindowRelativeMouseMode(object, enabled != 0) : 0;
}

void host_sdl_warp_mouse_in_window(uint32_t window, float x, float y)
{
	SDL_Window *object = handle_get(window, _handle_window);

	if (object)
		SDL_WarpMouseInWindow(object, x, y);
}

/* ---------- displays */

static void display_mode_to_guest(struct macos_display_mode *guest, const SDL_DisplayMode *mode)
{
	guest->display = mode->displayID;
	guest->format = (unsigned int)mode->format;
	guest->width = mode->w;
	guest->height = mode->h;
	guest->pixel_density = mode->pixel_density;
	guest->refresh_rate = mode->refresh_rate;
	guest->refresh_rate_numerator = mode->refresh_rate_numerator;
	guest->refresh_rate_denominator = mode->refresh_rate_denominator;
}

uint32_t host_sdl_display_for_window(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? SDL_GetDisplayForWindow(object) : 0;
}

uint32_t host_sdl_primary_display(void)
{
	return SDL_GetPrimaryDisplay();
}

int host_sdl_display_mode(uint32_t display, int which, struct macos_display_mode *mode)
{
	const SDL_DisplayMode *host = which ? SDL_GetCurrentDisplayMode(display) : SDL_GetDesktopDisplayMode(display);

	if (!host)
		return 0;
	display_mode_to_guest(mode, host);
	return 1;
}

int host_sdl_fullscreen_display_modes(uint32_t display, struct macos_display_mode *modes, int capacity)
{
	int count = 0, index;
	SDL_DisplayMode **list = SDL_GetFullscreenDisplayModes(display, &count);

	if (!list)
		return 0;
	if (count > capacity)
		count = capacity;
	for (index = 0; index < count; index++)
		display_mode_to_guest(&modes[index], list[index]);
	SDL_free(list);
	return count;
}

int host_sdl_closest_fullscreen_display_mode(uint32_t display, int width, int height, float refresh_rate,
	int include_high_density, struct macos_display_mode *mode)
{
	SDL_DisplayMode closest;

	if (!SDL_GetClosestFullscreenDisplayMode(display, width, height, refresh_rate, include_high_density != 0, &closest))
		return 0;
	display_mode_to_guest(mode, &closest);
	return 1;
}

/* the guest's copy of a mode has no pointer to SDL's own data, which the
mode SDL sets needs: the display's mode nearest it is the one set */
int host_sdl_set_window_fullscreen_mode(uint32_t window, int has_mode, const struct macos_display_mode *mode)
{
	SDL_Window *object = handle_get(window, _handle_window);
	SDL_DisplayMode closest;

	if (!object)
		return 0;
	if (!has_mode)
		return SDL_SetWindowFullscreenMode(object, NULL);
	if (!SDL_GetClosestFullscreenDisplayMode(mode->display, mode->width, mode->height, mode->refresh_rate,
			mode->pixel_density > 1.0f, &closest))
	{
		host_logf(HOST_LOG_WARN, "no fullscreen mode near %dx%d: %s", mode->width, mode->height, SDL_GetError());
		return SDL_SetWindowFullscreenMode(object, NULL);
	}
	return SDL_SetWindowFullscreenMode(object, &closest);
}

int host_sdl_display_usable_bounds(uint32_t display, int *rectangle)
{
	SDL_Rect bounds;

	if (!SDL_GetDisplayUsableBounds(display, &bounds))
		return 0;
	rectangle[0] = bounds.x;
	rectangle[1] = bounds.y;
	rectangle[2] = bounds.w;
	rectangle[3] = bounds.h;
	return 1;
}

/* ---------- OpenGL: the context is the host's choice (host_gl.c) */

int host_sdl_gl_set_attribute(int attribute, int value)
{
	switch ((SDL_GLAttr)attribute)
	{
	case SDL_GL_CONTEXT_PROFILE_MASK:
	case SDL_GL_CONTEXT_MAJOR_VERSION:
	case SDL_GL_CONTEXT_MINOR_VERSION:
	case SDL_GL_CONTEXT_FLAGS:
		/* the guest asks for OpenGL ES; host_gl_prepare_context sets these */
		return 1;
	default:
		return SDL_GL_SetAttribute((SDL_GLAttr)attribute, value);
	}
}

uint32_t host_sdl_gl_create_context(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);
	SDL_GLContext context;

	if (!object)
		return 0;
	host_gl_prepare_context();
	context = SDL_GL_CreateContext(object);
	if (!context)
	{
		host_logf(HOST_LOG_ERROR, "SDL_GL_CreateContext: %s", SDL_GetError());
		return 0;
	}
	host_gl_context_created();
	return handle_new(_handle_context, context);
}

int host_sdl_gl_make_current(uint32_t window, uint32_t context)
{
	return SDL_GL_MakeCurrent(handle_get(window, _handle_window), handle_get(context, _handle_context));
}

int host_sdl_gl_set_swap_interval(int interval)
{
	return SDL_GL_SetSwapInterval(interval);
}

int host_sdl_gl_swap_window(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? SDL_GL_SwapWindow(object) : 0;
}

/* ---------- events */

int host_sdl_poll_event(void *event)
{
	SDL_Event host_event;

	if (!SDL_PollEvent(&host_event))
		return 0;
	/* the layouts agree except for the pointers of text, drop and user
	events, which the platform layer does not read */
	memcpy(event, &host_event, sizeof(host_event));
	return 1;
}

void host_sdl_pump_events(void)
{
	SDL_PumpEvents();
}

int host_sdl_push_event(const void *event)
{
	SDL_Event host_event;

	memcpy(&host_event, event, sizeof(host_event));
	return SDL_PushEvent(&host_event) ? 1 : 0;
}

/* ---------- the 2D renderer */

uint32_t host_sdl_create_renderer(uint32_t window)
{
	SDL_Window *object = handle_get(window, _handle_window);

	return object ? handle_new(_handle_renderer, SDL_CreateRenderer(object, NULL)) : 0;
}

void host_sdl_destroy_renderer(uint32_t renderer)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	if (!object)
		return;
	handle_release(renderer);
	SDL_DestroyRenderer(object);
}

int host_sdl_set_render_vsync(uint32_t renderer, int vsync)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_SetRenderVSync(object, vsync) : 0;
}

int host_sdl_set_render_draw_color(uint32_t renderer, int red, int green, int blue, int alpha)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_SetRenderDrawColor(object, (Uint8)red, (Uint8)green, (Uint8)blue, (Uint8)alpha) : 0;
}

int host_sdl_render_clear(uint32_t renderer)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_RenderClear(object) : 0;
}

int host_sdl_set_render_scale(uint32_t renderer, float x, float y)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_SetRenderScale(object, x, y) : 0;
}

int host_sdl_render_debug_text(uint32_t renderer, float x, float y, const char *text)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_RenderDebugText(object, x, y, text) : 0;
}

/* a negative width: the whole target (SDL's NULL rectangle) */
int host_sdl_render_fill_rect(uint32_t renderer, float x, float y, float width, float height)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);
	SDL_FRect rectangle = { x, y, width, height };

	return object ? SDL_RenderFillRect(object, width < 0.0f ? NULL : &rectangle) : 0;
}

int host_sdl_render_present(uint32_t renderer)
{
	SDL_Renderer *object = handle_get(renderer, _handle_renderer);

	return object ? SDL_RenderPresent(object) : 0;
}

/* ---------- gamepads */

int host_sdl_get_gamepads(uint32_t *ids, int capacity)
{
	int count = 0, index;
	SDL_JoystickID *list = SDL_GetGamepads(&count);

	if (!list)
		return 0;
	if (count > capacity)
		count = capacity;
	for (index = 0; index < count; index++)
		ids[index] = list[index];
	SDL_free(list);
	return count;
}

uint32_t host_sdl_open_gamepad(uint32_t id)
{
	SDL_Gamepad *gamepad = SDL_OpenGamepad((SDL_JoystickID)id);

	if (gamepad)
	{
		host_logf(HOST_LOG_INFO, "gamepad %u: %s (type %d, %04x:%04x)", (unsigned)id, SDL_GetGamepadName(gamepad),
			(int)SDL_GetGamepadType(gamepad), SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad));
	}
	return handle_new(_handle_gamepad, gamepad);
}

uint32_t host_sdl_gamepad_from_id(uint32_t id)
{
	return handle_new(_handle_gamepad, SDL_GetGamepadFromID((SDL_JoystickID)id));
}

int host_sdl_gamepad_axis(uint32_t gamepad, int axis)
{
	SDL_Gamepad *object = handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadAxis(object, (SDL_GamepadAxis)axis) : 0;
}

int host_sdl_gamepad_button(uint32_t gamepad, int button)
{
	SDL_Gamepad *object = handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadButton(object, (SDL_GamepadButton)button) : 0;
}

int host_sdl_gamepad_type(uint32_t gamepad)
{
	SDL_Gamepad *object = handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadType(object) : SDL_GAMEPAD_TYPE_UNKNOWN;
}

int host_sdl_rumble_gamepad(uint32_t gamepad, uint32_t low, uint32_t high, uint32_t milliseconds)
{
	SDL_Gamepad *object = handle_get(gamepad, _handle_gamepad);

	return object ? SDL_RumbleGamepad(object, (Uint16)low, (Uint16)high, milliseconds) : 0;
}

/* (the event thread, when the controller goes: sdl_platform.c) */
void host_sdl_close_gamepad(uint32_t gamepad)
{
	SDL_Gamepad *object = handle_get(gamepad, _handle_gamepad);

	if (object)
	{
		handle_release(gamepad);
		SDL_CloseGamepad(object);
	}
}

/* ---------- audio

SDL calls audio_callback on its own audio thread, which cannot run guest
code; it passes each request to the stream's thread (audio_thread), which
can, and waits for it to be done. SDL holds the stream's lock throughout, so
the audio the guest puts into the stream meanwhile is kept in the binding's
buffer instead, and put in by audio_callback once the guest is done
(port/android/host/host_sdl.c). */

struct audio_binding
{
	uint32_t handle;
	uint32_t callback;
	uint32_t userdata;
	pthread_mutex_t lock;
	pthread_cond_t requested;
	pthread_cond_t done;
	int pending;
	/* the stream is destroyed: the thread frees the binding and ends */
	int quit;
	int additional;
	int total;
	unsigned char *buffer;
	int buffer_length;
	int buffer_size;
};

static __thread struct audio_binding *calling_back;

static void audio_binding_free(struct audio_binding *binding)
{
	pthread_cond_destroy(&binding->done);
	pthread_cond_destroy(&binding->requested);
	pthread_mutex_destroy(&binding->lock);
	SDL_free(binding->buffer);
	SDL_free(binding);
}

static void *audio_thread(void *context)
{
	struct audio_binding *binding = context;

	pthread_mutex_lock(&binding->lock);
	for (;;)
	{
		int additional, total;

		while (!binding->pending && !binding->quit)
			pthread_cond_wait(&binding->requested, &binding->lock);
		if (binding->quit)
			break;
		additional = binding->additional;
		total = binding->total;
		pthread_mutex_unlock(&binding->lock);
		calling_back = binding;
		host_call_guest(binding->callback, binding->userdata, binding->handle, (uint32_t)additional, (uint32_t)total);
		calling_back = NULL;
		pthread_mutex_lock(&binding->lock);
		binding->pending = 0;
		pthread_cond_signal(&binding->done);
	}
	pthread_mutex_unlock(&binding->lock);
	audio_binding_free(binding);
	return NULL;
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
	struct audio_binding *binding = userdata;

	pthread_mutex_lock(&binding->lock);
	binding->additional = additional;
	binding->total = total;
	binding->pending = 1;
	pthread_cond_signal(&binding->requested);
	while (binding->pending)
		pthread_cond_wait(&binding->done, &binding->lock);
	pthread_mutex_unlock(&binding->lock);
	if (binding->buffer_length)
	{
		SDL_PutAudioStreamData(stream, binding->buffer, binding->buffer_length);
		binding->buffer_length = 0;
	}
}

static int audio_keep(struct audio_binding *binding, const void *data, int length)
{
	if (length < 0)
		return 0;
	if (binding->buffer_length + length > binding->buffer_size)
	{
		int size = (binding->buffer_length + length) * 2;
		unsigned char *buffer = SDL_realloc(binding->buffer, (size_t)size);

		if (!buffer)
			return 0;
		binding->buffer = buffer;
		binding->buffer_size = size;
	}
	memcpy(binding->buffer + binding->buffer_length, data, (size_t)length);
	binding->buffer_length += length;
	return 1;
}

/* spec: an SDL_AudioSpec, three 32-bit members in both ABIs */
uint32_t host_sdl_open_audio_stream(uint32_t device, const void *spec, uint32_t callback, uint32_t userdata)
{
	struct audio_binding *binding = SDL_calloc(1, sizeof(*binding));
	SDL_AudioStream *stream;

	if (!binding)
		return 0;
	binding->callback = callback;
	binding->userdata = userdata;
	pthread_mutex_init(&binding->lock, NULL);
	pthread_cond_init(&binding->requested, NULL);
	pthread_cond_init(&binding->done, NULL);
	stream = SDL_OpenAudioDeviceStream((SDL_AudioDeviceID)device, spec, callback ? audio_callback : NULL, binding);
	if (!stream)
	{
		host_logf(HOST_LOG_ERROR, "SDL_OpenAudioDeviceStream: %s", SDL_GetError());
		SDL_free(binding);
		return 0;
	}
	/* the device starts paused, so no callback can run before this */
	binding->handle = handle_new(_handle_audio, stream);
	handle_context_set(binding->handle, binding);
	if (callback && host_native_thread_create(audio_thread, binding, 256 * 1024) != 0)
		host_fatal("cannot start the audio thread");
	return binding->handle;
}

int host_sdl_put_audio_stream_data(uint32_t stream, const void *data, int length)
{
	SDL_AudioStream *object = handle_get(stream, _handle_audio);

	if (!object)
		return 0;
	if (calling_back && calling_back->handle == stream)
		return audio_keep(calling_back, data, length);
	return SDL_PutAudioStreamData(object, data, length);
}

int host_sdl_resume_audio_stream_device(uint32_t stream)
{
	SDL_AudioStream *object = handle_get(stream, _handle_audio);

	return object ? SDL_ResumeAudioStreamDevice(object) : 0;
}

/* (voice chat's microphone: port/linux/src/voice_audio.c) */
int host_sdl_get_audio_stream_data(uint32_t stream, void *data, int length)
{
	SDL_AudioStream *object = handle_get(stream, _handle_audio);

	return object ? SDL_GetAudioStreamData(object, data, length) : -1;
}

int host_sdl_get_audio_stream_available(uint32_t stream)
{
	SDL_AudioStream *object = handle_get(stream, _handle_audio);

	return object ? SDL_GetAudioStreamAvailable(object) : -1;
}

/* (a stream with a callback has a thread the guest's callbacks run on:
once no callback can run any more, it is told to end, and frees the
binding and its stack as it goes) */
void host_sdl_destroy_audio_stream(uint32_t stream)
{
	struct audio_binding *binding = handle_context_get(stream);
	SDL_AudioStream *object = handle_get(stream, _handle_audio);

	if (!object)
		return;
	handle_release(stream);
	/* (returns once a callback that is running has finished) */
	SDL_DestroyAudioStream(object);
	if (!binding)
		return;
	if (!binding->callback)
	{
		audio_binding_free(binding);
		return;
	}
	pthread_mutex_lock(&binding->lock);
	binding->quit = 1;
	pthread_cond_signal(&binding->requested);
	pthread_mutex_unlock(&binding->lock);
}

/* ---------- the clipboard */

int host_sdl_set_clipboard_text(const char *text)
{
	return SDL_SetClipboardText(text) ? 1 : 0;
}

void host_sdl_get_clipboard_text(char *buffer, uint32_t size)
{
	char *text = SDL_GetClipboardText();

	SDL_strlcpy(buffer, text ? text : "", size);
	SDL_free(text);
}

/* ---------- messages and dialogs */

/* (Android's; the macOS guest does not call it) */
int host_sdl_show_toast(const char *message, int duration, int gravity, int x, int y)
{
	(void)duration;
	(void)gravity;
	(void)x;
	(void)y;
	host_logf(HOST_LOG_INFO, "%s", message);
	return 1;
}

int host_sdl_show_simple_message_box(uint32_t flags, const char *title, const char *message)
{
	return SDL_ShowSimpleMessageBox((SDL_MessageBoxFlags)flags, title, message, NULL) ? 1 : 0;
}

int host_sdl_show_message_box(uint32_t flags, const char *title, const char *message, int button_count,
	const int *button_ids, const uint32_t *button_flags, const char *button_texts, int *answer)
{
	SDL_MessageBoxButtonData buttons[8];
	SDL_MessageBoxData data;
	int index;

	if (button_count < 0 || button_count > 8)
		return 0;
	for (index = 0; index < button_count; index++)
	{
		buttons[index].flags = (SDL_MessageBoxButtonFlags)button_flags[index];
		buttons[index].buttonID = button_ids[index];
		buttons[index].text = button_texts;
		button_texts += strlen(button_texts) + 1;
	}
	memset(&data, 0, sizeof(data));
	data.flags = (SDL_MessageBoxFlags)flags;
	data.title = title;
	data.message = message;
	data.numbuttons = button_count;
	data.buttons = buttons;
	if (!SDL_ShowMessageBox(&data, answer))
	{
		host_logf(HOST_LOG_ERROR, "message box \"%s\": %s", title, SDL_GetError());
		return 0;
	}
	host_logf(HOST_LOG_INFO, "message box \"%s\": button %d", title, *answer);
	return 1;
}

struct file_choice
{
	SDL_AtomicInt done;
	char *path;
	uint32_t size;
	int chosen;
};

static void SDLCALL file_chosen(void *userdata, const char *const *files, int filter)
{
	struct file_choice *choice = userdata;

	(void)filter;
	if (files && files[0])
	{
		SDL_strlcpy(choice->path, files[0], choice->size);
		choice->chosen = 1;
	}
	else if (!files)
	{
		host_logf(HOST_LOG_ERROR, "the file dialog failed: %s", SDL_GetError());
	}
	SDL_SetAtomicInt(&choice->done, 1);
}

/* the dialog answers through events on the main thread, where this runs:
pump them until it has */
int host_sdl_choose_open_file(const char *filter_names, const char *filter_patterns, int filter_count, char *path,
	uint32_t size)
{
	SDL_DialogFileFilter filters[8];
	struct file_choice choice;
	int index;

	if (filter_count < 0 || filter_count > 8 || !size)
		return 0;
	for (index = 0; index < filter_count; index++)
	{
		filters[index].name = filter_names;
		filters[index].pattern = filter_patterns;
		filter_names += strlen(filter_names) + 1;
		filter_patterns += strlen(filter_patterns) + 1;
	}
	memset(&choice, 0, sizeof(choice));
	choice.path = path;
	choice.size = size;
	path[0] = 0;
	SDL_ShowOpenFileDialog(file_chosen, &choice, NULL, filter_count ? filters : NULL, filter_count, NULL, false);
	while (!SDL_GetAtomicInt(&choice.done))
	{
		SDL_PumpEvents();
		SDL_Delay(10);
	}
	return choice.chosen;
}
