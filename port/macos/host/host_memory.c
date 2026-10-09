/*
HOST_MEMORY.C

The guest's address space for the macOS port: a 4 GB arena aligned to 4 GB
(macos_host.h). A macOS arm64 process cannot map the low 4 GB itself
(port/macos/spike/lowmem_probe.c), so the arena stands in for it; the guest's
lifted code reaches it through x27 (tools/macos_asm_lift.py).

The arena is reserved inaccessible at start-up and its pages are handed out
here, at the 16 KB page size of Apple Silicon:

- the Xbox contiguous window at 0x80000000 and the image's range, at their
  fixed guest addresses;
- the guest's fixed mappings elsewhere (the Custom Edition tag cache at
  0x40440000, xbox_memory.c), and everything else (malloc arenas, thread
  stacks) from free ranges, above the image first as on Android
  (port/android/host/host_memory.c), so that the low fixed ranges stay free.

The Xbox code works in 4 KB pages, inside the window. A mapping of whole
16 KB pages is a fresh one (zeroed, and not backed until touched); the
partial pages at its ends are made accessible and zeroed in place. A
read-only protection covers whole 16 KB pages only, so as not to protect a
writable neighbour; the Xbox's own record of protections is xbox_memory.c's.

This file also implements the write tracking of
port/linux/src/memory_watch.c at 16 KB granularity, and reports crashes with
guest addresses. The treatment of Apple's pages comes from the iOS port by
Nicholas Dominici (github.com/NicholasDominici/halo-ce-ios,
port/ios/host/host_memory.c, CC0 1.0).
*/

#include "macos_host.h"

#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#define ARENA_SIZE 0x100000000ull
#define PAGE 0x4000ull
#define PAGE_COUNT (ARENA_SIZE / PAGE)
/* nothing below it is handed out: a null guest pointer must fault */
#define LOW_START 0x01000000ull

/* the guest's mmap flags and errno values (Linux's) */
#define LINUX_MAP_SHARED 0x01
#define LINUX_MAP_FIXED 0x10
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_MAP_FIXED_NOREPLACE 0x100000
#define LINUX_EINVAL 22
#define LINUX_ENOMEM 12
#define LINUX_EEXIST 17
#define LINUX_EIO 5

enum
{
	_page_free = 0,
	_page_mapped,
	/* the window, the image and the low guard: never handed out */
	_page_fixed,
};

uintptr_t host_arena;
static uint8_t page_state[PAGE_COUNT];
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t image_base, image_end;

static uint64_t round_down(uint64_t value)
{
	return value & ~(PAGE - 1);
}

static uint64_t round_up(uint64_t value)
{
	return (value + PAGE - 1) & ~(PAGE - 1);
}

static int in_window(uint64_t address, uint64_t size)
{
	return address >= HALO_GUEST_WINDOW_BASE &&
		address + size <= (uint64_t)HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE;
}

static void mark(uint64_t address, uint64_t size, uint8_t state)
{
	memset(&page_state[address / PAGE], state, (size_t)(round_up(address + size) - round_down(address)) / PAGE);
}

int host_memory_initialize(uint32_t base, uint32_t end)
{
	vm_address_t arena = 0;
	kern_return_t result = vm_map(mach_task_self(), &arena, ARENA_SIZE, ARENA_SIZE - 1, VM_FLAGS_ANYWHERE,
		MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_NONE);

	if (result != KERN_SUCCESS || (arena & (ARENA_SIZE - 1)))
	{
		host_logf(HOST_LOG_ERROR, "cannot reserve the guest's 4 GB arena (aligned to 4 GB): %d", result);
		return -1;
	}
	host_arena = arena;
	image_base = base;
	image_end = round_up(end);
	mark(0, LOW_START, _page_fixed);
	mark(HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_SIZE, _page_fixed);
	mark(image_base, image_end - image_base, _page_fixed);
	host_logf(HOST_LOG_INFO, "guest arena at %#llx (native pages of %d bytes)", (unsigned long long)arena,
		getpagesize());
	return 0;
}

/* makes [address, address + size) a fresh, zeroed mapping with protection;
0 on success */
static int map_fresh(uint64_t address, uint64_t size, int protection)
{
	uint64_t end = address + size;
	uint64_t inner_start = round_up(address), inner_end = round_down(end);

	if (inner_end > inner_start &&
		mmap(host_pointer(inner_start), inner_end - inner_start, protection, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
			-1, 0) != host_pointer(inner_start))
	{
		return -1;
	}
	/* the partial pages at the ends: accessible, and zeroed in place */
	if (address < inner_start)
	{
		uint64_t until = end < inner_start ? end : inner_start;

		if (mprotect(host_pointer(round_down(address)), PAGE, PROT_READ | PROT_WRITE))
			return -1;
		memset(host_pointer(address), 0, until - address);
	}
	if (end > inner_end && inner_end >= inner_start)
	{
		if (mprotect(host_pointer(inner_end), PAGE, PROT_READ | PROT_WRITE))
			return -1;
		memset(host_pointer(inner_end), 0, end - inner_end);
	}
	return 0;
}

/* a run of free pages at or above minimum and below limit; 0 if none */
static uint64_t find_free(uint64_t pages, uint64_t minimum, uint64_t limit)
{
	uint64_t run = 0, page;

	for (page = minimum / PAGE; page < limit / PAGE; page++)
	{
		run = page_state[page] == _page_free ? run + 1 : 0;
		if (run == pages)
			return (page + 1 - pages) * PAGE;
	}
	return 0;
}

void *host_low_map(size_t size, int protection)
{
	uint64_t length = round_up(size), address;

	if (!length || length > ARENA_SIZE)
		return NULL;
	pthread_mutex_lock(&memory_lock);
	address = find_free(length / PAGE, image_end, ARENA_SIZE);
	if (!address)
		address = find_free(length / PAGE, LOW_START, HALO_GUEST_WINDOW_BASE);
	if (address)
		mark(address, length, _page_mapped);
	pthread_mutex_unlock(&memory_lock);
	if (!address)
		return NULL;
	if (map_fresh(address, length, protection & ~PROT_EXEC))
	{
		host_low_unmap(host_pointer(address), length);
		return NULL;
	}
	return host_pointer(address);
}

void host_low_unmap(void *native, size_t size)
{
	uint64_t address = round_down(guest_pointer(native)), end = round_up((uint64_t)guest_pointer(native) + size);
	uint64_t page;

	pthread_mutex_lock(&memory_lock);
	for (page = address / PAGE; page < end / PAGE && page < PAGE_COUNT; page++)
	{
		if (page_state[page] != _page_mapped)
			continue;
		/* give the memory back, keeping the address space reserved */
		mmap(host_pointer(page * PAGE), PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		page_state[page] = _page_free;
	}
	pthread_mutex_unlock(&memory_lock);
}

int host_low_owns(uintptr_t address, size_t size)
{
	return host_arena && address >= host_arena && address - host_arena + size <= ARENA_SIZE;
}

/* ---------- the guest's memory system calls */

static long read_file(uint64_t address, uint64_t size, int fd, int64_t offset)
{
	uint64_t done = 0;

	while (done < size)
	{
		ssize_t count = pread(fd, (char *)host_pointer(address) + done, (size_t)(size - done), offset + (off_t)done);

		if (count < 0)
			return -LINUX_EIO;
		if (count == 0)
			break;
		done += (uint64_t)count;
	}
	return 0;
}

long host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset)
{
	int anonymous = (flags & LINUX_MAP_ANONYMOUS) || fd < 0;
	int fixed = flags & (LINUX_MAP_FIXED | LINUX_MAP_FIXED_NOREPLACE);

	protection &= PROT_READ | PROT_WRITE;
	if (!size || size > ARENA_SIZE)
		return -LINUX_EINVAL;
	if ((flags & LINUX_MAP_SHARED) && !anonymous)
	{
		host_logf(HOST_LOG_WARN, "guest: shared file mappings are not supported");
		return -LINUX_EINVAL;
	}
	if (fixed)
	{
		uint64_t page;
		int busy = 0;

		if (address + size > ARENA_SIZE || address < LOW_START)
			return -LINUX_ENOMEM;
		pthread_mutex_lock(&memory_lock);
		if (!in_window(address, size))
		{
			for (page = address / PAGE; page < round_up(address + size) / PAGE; page++)
			{
				if (page_state[page] == _page_fixed ||
					(page_state[page] == _page_mapped && (flags & LINUX_MAP_FIXED_NOREPLACE)))
				{
					busy = 1;
					break;
				}
			}
			if (!busy)
				mark(address, size, _page_mapped);
		}
		pthread_mutex_unlock(&memory_lock);
		if (busy)
			return -LINUX_EEXIST;
		/* readable and writable first, to read a file into it */
		if (map_fresh(address, size, anonymous ? protection : PROT_READ | PROT_WRITE))
			return -LINUX_ENOMEM;
	}
	else
	{
		void *native = host_low_map(size, anonymous ? protection : PROT_READ | PROT_WRITE);

		if (!native)
			return -LINUX_ENOMEM;
		address = guest_pointer(native);
	}
	if (!anonymous)
	{
		long result = read_file(address, size, fd, offset);

		if (result)
			return result;
		host_guest_mprotect(address, size, protection);
	}
	return (long)address;
}

long host_guest_munmap(uint64_t address, uint64_t size)
{
	uint64_t inner_start = round_up(address), inner_end = round_down(address + size);

	if (!size || address + size > ARENA_SIZE)
		return -LINUX_EINVAL;
	if (address < image_end && address + size > image_base)
		return -LINUX_EINVAL;
	if (in_window(address, size))
	{
		/* the window stays the guest's: its whole pages go back to the
		system, its partial ones stay as they are */
		if (inner_end > inner_start)
			mmap(host_pointer(inner_start), inner_end - inner_start, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
				-1, 0);
		return 0;
	}
	if (inner_end > inner_start)
		host_low_unmap(host_pointer(inner_start), inner_end - inner_start);
	return 0;
}

long host_guest_mprotect(uint64_t address, uint64_t size, int protection)
{
	uint64_t end = address + size;

	protection &= PROT_READ | PROT_WRITE;
	if (!size || end > ARENA_SIZE)
		return -LINUX_EINVAL;
	if (!(protection & PROT_WRITE))
	{
		/* only the whole pages: rounding outwards would also protect a
		writable neighbour (a 4 KB Xbox allocation beside it); the partial
		pages stay writable */
		uint64_t inner_start = round_up(address), inner_end = round_down(end);

		if (inner_end <= inner_start)
			return 0;
		return mprotect(host_pointer(inner_start), inner_end - inner_start, protection) ?
			-host_linux_errno(errno) : 0;
	}
	return mprotect(host_pointer(round_down(address)), round_up(end) - round_down(address), protection) ?
		-host_linux_errno(errno) : 0;
}

/* ---------- write tracking (port/linux/src/memory_watch.c), in the window */

#define WATCH_PAGE_COUNT (HALO_GUEST_WINDOW_SIZE / PAGE)

static uint32_t generations[WATCH_PAGE_COUNT];
static volatile uint8_t protected_pages[WATCH_PAGE_COUNT];
static uint8_t watched_pages[WATCH_PAGE_COUNT];
static unsigned page_locks[WATCH_PAGE_COUNT];
static uint32_t serial = 1;
static int watch_enabled;

/* A streaming thread can fault on a page while the render thread protects
it: the flag and the protection change together under the page's lock. The
lock never encloses an access to guest memory, so a fault cannot re-enter it. */
static void page_lock(unsigned page)
{
	while (__atomic_exchange_n(&page_locks[page], 1, __ATOMIC_ACQUIRE))
	{
	}
}

static void page_unlock(unsigned page)
{
	__atomic_store_n(&page_locks[page], 0, __ATOMIC_RELEASE);
}

static void mark_written(unsigned page)
{
	if (mprotect(host_pointer(HALO_GUEST_WINDOW_BASE + page * PAGE), PAGE, PROT_READ | PROT_WRITE))
		_exit(128 + SIGBUS);
	__atomic_store_n(&generations[page], __atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED), __ATOMIC_RELEASE);
	protected_pages[page] = 0;
}

static unsigned watch_first(uint32_t address)
{
	return (unsigned)((address - HALO_GUEST_WINDOW_BASE) / PAGE);
}

static unsigned watch_last(uint32_t address, uint32_t size)
{
	return (unsigned)(((uint64_t)address + size - 1 - HALO_GUEST_WINDOW_BASE) / PAGE);
}

void host_memory_watch_initialize(void)
{
	watch_enabled = 1;
}

void host_memory_watch_protect(uint32_t address, uint32_t size)
{
	unsigned page;

	if (!watch_enabled || !size || !in_window(address, size))
		return;
	for (page = watch_first(address); page <= watch_last(address, size); page++)
	{
		page_lock(page);
		if (!protected_pages[page])
		{
			watched_pages[page] = 1;
			if (!mprotect(host_pointer(HALO_GUEST_WINDOW_BASE + page * PAGE), PAGE, PROT_READ))
				protected_pages[page] = 1;
		}
		page_unlock(page);
	}
}

uint32_t host_memory_watch_serial(void)
{
	return __atomic_load_n(&serial, __ATOMIC_RELAXED);
}

uint32_t host_memory_watch_generation(uint32_t address, uint32_t size)
{
	uint32_t newest = 0;
	unsigned page;

	if (!size || !in_window(address, size))
		return 0;
	for (page = watch_first(address); page <= watch_last(address, size); page++)
	{
		uint32_t generation = __atomic_load_n(&generations[page], __ATOMIC_ACQUIRE);

		if (generation > newest)
			newest = generation;
	}
	return newest;
}

void host_memory_watch_prepare_write(uint32_t address, uint32_t size)
{
	unsigned page;

	if (!watch_enabled || !size || !in_window(address, size))
		return;
	for (page = watch_first(address); page <= watch_last(address, size); page++)
	{
		page_lock(page);
		if (protected_pages[page])
			mark_written(page);
		page_unlock(page);
	}
}

void host_memory_watch_forget(uint32_t address, uint32_t size)
{
	unsigned page;

	if (!size || !in_window(address, size))
		return;
	for (page = watch_first(address); page <= watch_last(address, size); page++)
	{
		page_lock(page);
		mark_written(page);
		page_unlock(page);
	}
}

/* ---------- faults */

static struct sigaction previous_segv, previous_bus, previous_ill;

static void report_crash(int signal_number, siginfo_t *information, void *context)
{
	const ucontext_t *ucontext = context;
	const struct __darwin_arm_thread_state64 *state = &ucontext->uc_mcontext->__ss;
	uint64_t pc = (uint64_t)__darwin_arm_thread_state64_get_pc(*state);
	uint64_t lr = (uint64_t)__darwin_arm_thread_state64_get_lr(*state);
	uint64_t fp = (uint64_t)__darwin_arm_thread_state64_get_fp(*state);
	uint64_t sp = (uint64_t)__darwin_arm_thread_state64_get_sp(*state);
	int index;

	host_logf(HOST_LOG_ERROR, "signal %d at address %p: pc %#llx lr %#llx sp %#llx", signal_number,
		information->si_addr, (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)sp);
	if (host_low_owns(pc, 4))
	{
		host_logf(HOST_LOG_ERROR, "  in the guest: llvm-symbolizer --obj=build/macos/halo_guest.elf 0x%x 0x%x",
			guest_pointer((void *)pc), guest_pointer((void *)lr));
	}
	for (index = 0; index < 28; index += 4)
	{
		host_logf(HOST_LOG_ERROR, "  x%-2d %016llx %016llx %016llx %016llx", index,
			(unsigned long long)state->__x[index], (unsigned long long)state->__x[index + 1],
			(unsigned long long)state->__x[index + 2], (unsigned long long)state->__x[index + 3]);
	}
	/* the frame records: fp and lr, 8 bytes each (guest frames are native
	addresses in the arena) */
	for (index = 0; index < 24 && fp && !(fp & 7) && host_low_owns(fp, 16); index++)
	{
		const uint64_t *frame = (const uint64_t *)fp;

		host_logf(HOST_LOG_ERROR, "  frame %2d: return 0x%x", index, guest_pointer((void *)frame[1]));
		if (frame[0] <= fp)
			break;
		fp = frame[0];
	}
}

static void chain(struct sigaction *previous, int signal_number, siginfo_t *information, void *context)
{
	sigaction(signal_number, previous, NULL);
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction)
			previous->sa_sigaction(signal_number, information, context);
	}
	else if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN)
	{
		previous->sa_handler(signal_number);
	}
	/* returning re-executes the faulting instruction under the previous (or
	the default) handler */
}

static void fault_handler(int signal_number, siginfo_t *information, void *context)
{
	uintptr_t address = (uintptr_t)information->si_addr;

	if (watch_enabled && host_low_owns(address, 1) && in_window(guest_pointer((void *)address), 1))
	{
		unsigned page = watch_first(guest_pointer((void *)address));

		page_lock(page);
		/* (a second writer may have repaired the page since this fault) */
		if (watched_pages[page])
		{
			mark_written(page);
			page_unlock(page);
			return;
		}
		page_unlock(page);
	}
	report_crash(signal_number, information, context);
	chain(signal_number == SIGBUS ? &previous_bus : signal_number == SIGSEGV ? &previous_segv : &previous_ill,
		signal_number, information, context);
}

void host_install_signal_handlers(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&action.sa_mask);
	action.sa_sigaction = fault_handler;
	sigaction(SIGSEGV, &action, &previous_segv);
	sigaction(SIGBUS, &action, &previous_bus);
	sigaction(SIGILL, &action, &previous_ill);
}
