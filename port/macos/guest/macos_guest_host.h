/*
MACOS_GUEST_HOST.H

The macOS host's services for the guest beyond the Android guest's
(port/android/guest/runtime/guest_host.h): the SDL calls of the desktop
platform layer, which the Android guest does not make. Each name here is
listed in port/macos/host_imports.list and defined in port/macos/host.

Parameter types follow port/android/include/halo_android_abi.h: 32-bit and
64-bit integers, floats and pointers, which arm64_32 passes zero-extended.
The host translates each guest pointer into the arena. Windows, renderers and
displays are small integer handles here, as in guest_host.h.
*/

#ifndef __HALO_MACOS_GUEST_HOST_H
#define __HALO_MACOS_GUEST_HOST_H

/* an SDL_DisplayMode without its pointer to SDL's own data, whose layout
would differ between the two ABIs: fixed-width members only */
struct macos_display_mode
{
	unsigned int display;
	unsigned int format;
	int width;
	int height;
	float pixel_density;
	float refresh_rate;
	int refresh_rate_numerator;
	int refresh_rate_denominator;
};

/* ---------- windows and displays (guest_sdl_desktop.c) */

void host_sdl_destroy_window(unsigned int window);
long long host_sdl_window_flags(unsigned int window);
void host_sdl_window_size(unsigned int window, int *width, int *height);
int host_sdl_set_window_size(unsigned int window, int width, int height);
int host_sdl_set_window_fullscreen(unsigned int window, int fullscreen);
/* has_mode 0: fullscreen over the desktop (SDL's NULL mode); else the
display's fullscreen mode nearest the one described */
int host_sdl_set_window_fullscreen_mode(unsigned int window, int has_mode, const struct macos_display_mode *mode);
void host_sdl_warp_mouse_in_window(unsigned int window, float x, float y);
unsigned int host_sdl_display_for_window(unsigned int window);
unsigned int host_sdl_primary_display(void);
/* which: 0 the desktop mode, 1 the current mode; 1 on success */
int host_sdl_display_mode(unsigned int display, int which, struct macos_display_mode *mode);
/* the display's fullscreen modes, up to capacity; returns how many */
int host_sdl_fullscreen_display_modes(unsigned int display, struct macos_display_mode *modes, int capacity);
int host_sdl_closest_fullscreen_display_mode(unsigned int display, int width, int height, float refresh_rate,
	int include_high_density, struct macos_display_mode *mode);
/* x, y, w, h */
int host_sdl_display_usable_bounds(unsigned int display, int *rectangle);

/* ---------- events */

void host_sdl_pump_events(void);
/* an SDL_Event of a type that carries no pointer (128 bytes, the same in
both ABIs) */
int host_sdl_push_event(const void *event);

/* ---------- SDL's 2D renderer (the progress window of the maps' extraction,
sdl_platform.c) */

unsigned int host_sdl_create_renderer(unsigned int window);
void host_sdl_destroy_renderer(unsigned int renderer);
int host_sdl_set_render_vsync(unsigned int renderer, int vsync);
int host_sdl_set_render_draw_color(unsigned int renderer, int red, int green, int blue, int alpha);
int host_sdl_render_clear(unsigned int renderer);
int host_sdl_set_render_scale(unsigned int renderer, float x, float y);
int host_sdl_render_debug_text(unsigned int renderer, float x, float y, const char *text);
int host_sdl_render_fill_rect(unsigned int renderer, float x, float y, float width, float height);
int host_sdl_render_present(unsigned int renderer);

/* ---------- errors: what SDL_GetError returns after a guest-side failure */

void host_sdl_set_error(const char *text);

/* ---------- dialogs */

/* the buttons' texts are NUL-separated; *answer is the chosen button's id */
int host_sdl_show_message_box(unsigned int flags, const char *title, const char *message, int button_count,
	const int *button_ids, const unsigned int *button_flags, const char *button_texts, int *answer);
/* asks for one file to open and waits for the answer: 1 with its path, 0 if
none was chosen. The filters' names and patterns are NUL-separated */
int host_sdl_choose_open_file(const char *filter_names, const char *filter_patterns, int filter_count,
	char *path, unsigned int size);

#endif
