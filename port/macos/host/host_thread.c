/*
HOST_THREAD.C

Threads that run guest code (as port/android/host/host_thread.c).

Guest code keeps stack addresses in 32-bit registers, so every thread that
runs it needs its stack in the arena; the host enters guest code with the
arena's base in x27 (guest_call.S). Guest threads are made here, with their
stacks given to pthread_create. The main thread, which SDL needs for windows
and events, switches to an arena stack to run the game's main()
(host_run_guest_main). SDL's audio thread hands its callback to a thread made
here (host_sdl.c). The guest's thread pointer (its musl struct pthread) is
kept per thread in host TLS.

Thread stacks are freed by a reaper thread once their thread has exited.
*/

#include "macos_host.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define GUARD_SIZE 0x4000
#define MAIN_STACK_SIZE (16 * 1024 * 1024)

static __thread uint32_t guest_tp;

uint32_t host_get_tp(void)
{
	return guest_tp;
}

void host_set_tp(uint32_t thread)
{
	guest_tp = thread;
}

/* ---------- stacks */

static void *stack_allocate(size_t size, void **mapping, size_t *mapping_size)
{
	size_t total = size + GUARD_SIZE;
	char *base = host_low_map(total, PROT_READ | PROT_WRITE);

	if (!base)
		return NULL;
	/* a guard page at the bottom */
	mprotect(base, GUARD_SIZE, PROT_NONE);
	*mapping = base;
	*mapping_size = total;
	return base + GUARD_SIZE;
}

static int on_arena_stack(void)
{
	return host_low_owns((uintptr_t)__builtin_frame_address(0), 1);
}

/* ---------- calling into the guest */

uint32_t host_call_guest(uint32_t function, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	if (!on_arena_stack())
		host_fatal("guest code called on a thread without a stack in the guest's memory");
	if (!guest_tp)
		host_guest_invoke((uintptr_t)host_pointer(host_image.header->thread_attach), 0, 0, 0, 0, host_arena);
	return host_guest_invoke((uintptr_t)host_pointer(function), a, b, c, d, host_arena);
}

void host_run_guest_main(uint32_t boot)
{
	void *mapping;
	size_t mapping_size;
	char *stack = stack_allocate(MAIN_STACK_SIZE, &mapping, &mapping_size);

	if (!stack)
		host_fatal("cannot allocate the game's main stack");
	host_guest_on_stack((uintptr_t)host_pointer(host_image.header->start), boot, host_arena, stack + MAIN_STACK_SIZE);
	host_fatal("the game returned from its entry point");
}

/* ---------- threads */

struct thread_start
{
	void *(*function)(void *);
	void *argument;
	void *mapping;
	size_t mapping_size;
};

struct finished_thread
{
	struct finished_thread *next;
	pthread_t thread;
	void *mapping;
	size_t mapping_size;
};

static pthread_mutex_t reaper_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reaper_condition = PTHREAD_COND_INITIALIZER;
static struct finished_thread *finished_threads;
static int reaper_started;

static void *reaper(void *unused)
{
	(void)unused;
	for (;;)
	{
		struct finished_thread *finished;

		pthread_mutex_lock(&reaper_lock);
		while (!finished_threads)
			pthread_cond_wait(&reaper_condition, &reaper_lock);
		finished = finished_threads;
		finished_threads = finished->next;
		pthread_mutex_unlock(&reaper_lock);
		pthread_join(finished->thread, NULL);
		host_low_unmap(finished->mapping, finished->mapping_size);
		free(finished);
	}
	return NULL;
}

static void *thread_main(void *context)
{
	struct thread_start start = *(struct thread_start *)context;
	struct finished_thread *finished;

	free(context);
	host_debug_thread_started();
	start.function(start.argument);
	host_debug_thread_exited();
	guest_tp = 0;

	finished = calloc(1, sizeof(*finished));
	if (!finished)
		return NULL;
	finished->thread = pthread_self();
	finished->mapping = start.mapping;
	finished->mapping_size = start.mapping_size;
	pthread_mutex_lock(&reaper_lock);
	finished->next = finished_threads;
	finished_threads = finished;
	pthread_cond_signal(&reaper_condition);
	pthread_mutex_unlock(&reaper_lock);
	return NULL;
}

int host_native_thread_create(void *(*function)(void *), void *argument, size_t stack_size)
{
	struct thread_start *start = calloc(1, sizeof(*start));
	pthread_attr_t attributes;
	pthread_t thread;
	void *stack;
	int error;

	if (!start)
		return ENOMEM;
	pthread_mutex_lock(&reaper_lock);
	if (!reaper_started)
	{
		pthread_t reaper_thread;

		if (pthread_create(&reaper_thread, NULL, reaper, NULL) == 0)
		{
			pthread_detach(reaper_thread);
			reaper_started = 1;
		}
	}
	pthread_mutex_unlock(&reaper_lock);

	stack_size = (stack_size + 0xffff) & ~(size_t)0xffff;
	stack = stack_allocate(stack_size, &start->mapping, &start->mapping_size);
	if (!stack)
	{
		free(start);
		return EAGAIN;
	}
	start->function = function;
	start->argument = argument;
	pthread_attr_init(&attributes);
	pthread_attr_setstack(&attributes, stack, stack_size);
	error = pthread_create(&thread, &attributes, thread_main, start);
	pthread_attr_destroy(&attributes);
	if (error)
	{
		host_low_unmap(start->mapping, start->mapping_size);
		free(start);
	}
	return error;
}

static void *guest_thread_main(void *guest_thread)
{
	host_call_guest(host_image.header->thread_start, (uint32_t)(uintptr_t)guest_thread, 0, 0, 0);
	return NULL;
}

int host_thread_create(uint32_t guest_thread, uint32_t stack_size)
{
	return host_native_thread_create(guest_thread_main, (void *)(uintptr_t)guest_thread, stack_size);
}

/* ---------- the debugger's view (port/android/host/host_debug.c keeps a
registry for its sampler; the macOS port leaves sampling to Instruments) */

void host_debug_thread_started(void)
{
}

void host_debug_thread_exited(void)
{
}
