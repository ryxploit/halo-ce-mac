/*
HOST_MAIN.C

Entry point of the macOS port.

It reserves the guest's arena, loads the guest image embedded in this
executable (host_loader.c), gives the game an environment describing where
its files live, and runs the game's main() on the main thread, on a stack in
the arena (host_thread.c): SDL and Cocoa need windows, events and dialogs on
the main thread.

Storage: the app bundle is read-only, so everything the game writes is in
the user's folder, ~/Library/Application Support/Halo CE (HALO_MACOS_USER_ROOT
overrides it): config.toml, the maps folder extracted from the player's disc
image, debug.txt, the saves (saves/) and this host's log (macos-host.log).
The guest finds them there: its SDL_GetBasePath and its executable's path
are in that folder (port/macos/guest/guest_sdl_desktop.c, host_syscall.c),
and it is the current directory.

--check loads the image and exits without starting the game, as a smoke test
that needs no display and no game data.
*/

#include "macos_host.h"
#include "guest_image.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ENVIRONMENT_MAXIMUM 64

static char user_root[1024];
static FILE *log_file;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- logging and termination */

static const char *priority_name(int priority)
{
	return priority >= HOST_LOG_ERROR ? "error" : priority == HOST_LOG_WARN ? "warning" : "info";
}

static void log_line(int priority, const char *text)
{
	pthread_mutex_lock(&log_lock);
	fprintf(stderr, "halo: %s: %s\n", priority_name(priority), text);
	if (log_file)
	{
		fprintf(log_file, "%s: %s\n", priority_name(priority), text);
		fflush(log_file);
	}
	pthread_mutex_unlock(&log_lock);
}

void host_logf(int priority, const char *format, ...)
{
	char text[2048];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	log_line(priority, text);
}

/* the guest's log (guest_host.h) */
void host_log(int priority, const char *text)
{
	log_line(priority, text);
}

void host_fatal(const char *format, ...)
{
	char message[2048];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	log_line(HOST_LOG_ERROR, message);
	if (!getenv("HALO_MACOS_NO_DIALOGS"))
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo CE", message, NULL);
	_exit(1);
}

void host_abort(const char *reason)
{
	host_fatal("the game stopped: %s", reason);
}

void host_exit(int code)
{
	host_logf(HOST_LOG_INFO, "the game exited (%d)", code);
	if (log_file)
		fflush(log_file);
	_exit(code);
}

/* ---------- paths */

const char *host_user_root(void)
{
	return user_root;
}

/* (Android's; the macOS guest does not call it) */
void host_android_path(int which, char *buffer, uint32_t size)
{
	snprintf(buffer, size, which ? "%s/saves" : "%s", user_root);
}

static int make_directories(const char *path)
{
	char partial[1024];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if (partial[index] != '/')
			continue;
		partial[index] = 0;
		if (mkdir(partial, 0755) != 0 && errno != EEXIST)
			return -1;
		partial[index] = '/';
	}
	return mkdir(partial, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

static void user_root_initialize(void)
{
	const char *override = getenv("HALO_MACOS_USER_ROOT");
	const char *home = getenv("HOME");
	char path[1200];

	if (override && *override)
		snprintf(user_root, sizeof(user_root), "%s", override);
	else if (home && *home)
		snprintf(user_root, sizeof(user_root), "%s/Library/Application Support/Halo CE", home);
	else
		host_fatal("HOME is not set, so the user's folder cannot be found");
	if (make_directories(user_root) != 0)
		host_fatal("cannot create %s: %s", user_root, strerror(errno));
	snprintf(path, sizeof(path), "%s/macos-host.log", user_root);
	log_file = fopen(path, "w");
	if (chdir(user_root) != 0)
		host_fatal("cannot enter %s: %s", user_root, strerror(errno));
}

/* ---------- the guest's environment */

struct environment
{
	char *entries[ENVIRONMENT_MAXIMUM];
	int count;
};

static void environment_set(struct environment *environment, const char *name, const char *value)
{
	size_t length = strlen(name);
	char *entry = malloc(length + strlen(value) + 2);
	int index;

	if (!entry)
		return;
	sprintf(entry, "%s=%s", name, value);
	for (index = 0; index < environment->count; index++)
	{
		if (!strncmp(environment->entries[index], name, length) && environment->entries[index][length] == '=')
		{
			free(environment->entries[index]);
			environment->entries[index] = entry;
			return;
		}
	}
	if (environment->count < ENVIRONMENT_MAXIMUM)
		environment->entries[environment->count++] = entry;
	else
		free(entry);
}

/* POSIX TZ for the current local offset (the guest's musl has no zone
database), as port/android/host/host_main.c */
static void time_zone(char *buffer, size_t size)
{
	time_t now = time(NULL);
	struct tm local;
	long offset;

	localtime_r(&now, &local);
	offset = -local.tm_gmtoff;
	snprintf(buffer, size, "<L>%s%ld:%02ld", offset < 0 ? "-" : "", labs(offset) / 3600, (labs(offset) / 60) % 60);
}

/* copies argv and the environment into guest memory */
static uint32_t make_boot(const struct environment *environment)
{
	size_t size = 0x10000;
	char *memory = host_low_map(size, PROT_READ | PROT_WRITE);
	struct halo_guest_boot *boot = (struct halo_guest_boot *)memory;
	uint32_t *argv, *environment_list;
	char *strings;
	int index;

	if (!memory)
		host_fatal("cannot allocate the game's environment");
	argv = (uint32_t *)(memory + sizeof(*boot));
	environment_list = argv + 2;
	strings = (char *)(environment_list + ENVIRONMENT_MAXIMUM + 1);
	strcpy(strings, "halo");
	argv[0] = guest_pointer(strings);
	argv[1] = 0;
	strings += strlen(strings) + 1;
	for (index = 0; index < environment->count; index++)
	{
		size_t length = strlen(environment->entries[index]) + 1;

		if (strings + length > memory + size)
			break;
		memcpy(strings, environment->entries[index], length);
		environment_list[index] = guest_pointer(strings);
		strings += length;
	}
	environment_list[index] = 0;
	boot->argc = 1;
	boot->argv = guest_pointer(argv);
	boot->environment = guest_pointer(environment_list);
	boot->page_size = (uint32_t)getpagesize();
	return guest_pointer(boot);
}

/* ---------- main */

int main(int argc, char *argv[])
{
	struct environment environment = { { 0 }, 0 };
	char zone[64];
	int check = argc == 2 && !strcmp(argv[1], "--check");
	uint32_t boot;

	if (argc > 1 && !check)
	{
		fprintf(stderr, "usage: %s [--check]\n", argv[0]);
		return 2;
	}
	user_root_initialize();
	host_logf(HOST_LOG_INFO, "Halo CE for macOS (unofficial port) starting; user folder %s", user_root);
	host_install_signal_handlers();
	if (host_load_image() != 0)
		host_fatal("cannot load the game image; see %s/macos-host.log", user_root);
	if (check)
	{
		host_logf(HOST_LOG_INFO, "check: the game image loads; not starting it");
		return 0;
	}

	/* the settings' environment variables (HALO_EXIT_AFTER and the others of
	port/linux/src/port_config.c), for tests and for the player */
	{
		extern char **environ;
		char **entry;

		for (entry = environ; *entry; entry++)
		{
			const char *equals = strchr(*entry, '=');
			char name[128];

			if (strncmp(*entry, "HALO_", 5) || !equals || (size_t)(equals - *entry) >= sizeof(name))
				continue;
			memcpy(name, *entry, (size_t)(equals - *entry));
			name[equals - *entry] = 0;
			environment_set(&environment, name, equals + 1);
		}
	}
	environment_set(&environment, "HOME", user_root);
	environment_set(&environment, "HALO_MACOS_USER_ROOT", user_root);
	time_zone(zone, sizeof(zone));
	environment_set(&environment, "TZ", zone);
	boot = make_boot(&environment);
	host_run_guest_main(boot);
}
