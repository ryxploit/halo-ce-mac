/*
ARENA_PROBE.C

Host half of the arena probe: runs the lifted ILP32 guest of arena_guest.c
in-process, as the game will run (docs/macos-port-audit.md). It reserves a
4 GB arena aligned to 4 GB, aliases the guest's signed code read/execute at
guest address 0x88000000 with vm_remap, copies the guest's data, fills its
import table and calls it on stacks inside the arena.

Build: ninja macos_arena_probe. Run: build/macos/arena_probe. The exit status
is 0 when every check passes.
*/

#include <mach/mach.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "guest_image.h"

#define ARENA_SIZE 0x100000000ull
#define IMAGE_BASE 0x88000000u
#define PROBE_MAGIC 0x45424f50u
#define STACK_BASE 0x01000000u
#define STACK_SIZE 0x00100000u
#define PAGE 0x4000u

uint32_t host_guest_invoke(uintptr_t function, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uintptr_t arena);
uint64_t host_guest_on_stack(uintptr_t function, uint32_t argument, uintptr_t arena, void *stack_top);

struct probe_header
{
	uint32_t magic;
	uint32_t layout;
	uint32_t imports;
	uint32_t callback;
	uint32_t atomic_add;
	uint32_t atomic_value;
	uint32_t import_table;
	uint32_t import_names;
	uint32_t import_count;
};

static uintptr_t arena;
static const struct probe_header *header;

static void *host_pointer(uint64_t guest)
{
	return guest ? (void *)(arena | (uint32_t)guest) : NULL;
}

static uintptr_t guest_function(uint32_t guest)
{
	return arena | guest;
}

/* ---------- imports: guest pointers arrive as 32-bit offsets */

static uint32_t host_probe_sum(uint64_t values, uint32_t count)
{
	const uint32_t *native = host_pointer(values);
	uint32_t sum = 0;

	for (uint32_t i = 0; i < count; i++)
		sum += native[i];
	return sum;
}

static void host_probe_write(uint64_t buffer, uint32_t size)
{
	snprintf(host_pointer(buffer), size, "macos");
}

static const struct
{
	const char *name;
	void *function;
} imports[] = {
	{"host_probe_sum", (void *)host_probe_sum},
	{"host_probe_write", (void *)host_probe_write},
};

static int load(void)
{
	vm_address_t reserved = 0, code;
	vm_prot_t current = 0, maximum = 0;
	kern_return_t result;
	uint64_t *table;
	const char *name;
	uint32_t count;

	result = vm_map(mach_task_self(), &reserved, ARENA_SIZE, ARENA_SIZE - 1, VM_FLAGS_ANYWHERE, MACH_PORT_NULL, 0,
		FALSE, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_NONE);
	if (result != KERN_SUCCESS || (reserved & (ARENA_SIZE - 1)))
	{
		fprintf(stderr, "cannot reserve an aligned 4 GB arena: %d\n", result);
		return -1;
	}
	arena = reserved;
	code = arena + IMAGE_BASE;
	result = vm_remap(mach_task_self(), &code, MACOS_GUEST_CODE_SIZE, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
		mach_task_self(), (vm_address_t)halo_guest_code, FALSE, &current, &maximum, VM_INHERIT_COPY);
	if (result != KERN_SUCCESS || !(current & VM_PROT_EXECUTE) || (current & VM_PROT_WRITE))
	{
		fprintf(stderr, "cannot alias the signed guest code: %d (protection %d)\n", result, current);
		return -1;
	}
	if (mprotect(host_pointer(MACOS_GUEST_DATA_ADDRESS), MACOS_GUEST_IMAGE_END - MACOS_GUEST_DATA_ADDRESS,
			PROT_READ | PROT_WRITE))
	{
		perror("mprotect guest data");
		return -1;
	}
	memcpy(host_pointer(MACOS_GUEST_DATA_ADDRESS), halo_guest_data, MACOS_GUEST_DATA_SIZE);
	if (mprotect(host_pointer(STACK_BASE), 4 * STACK_SIZE, PROT_READ | PROT_WRITE))
	{
		perror("mprotect guest stacks");
		return -1;
	}
	header = host_pointer(IMAGE_BASE);
	if (header->magic != PROBE_MAGIC)
	{
		fprintf(stderr, "bad guest header magic 0x%x\n", header->magic);
		return -1;
	}
	table = host_pointer(header->import_table);
	name = host_pointer(header->import_names);
	count = *(const uint32_t *)host_pointer(header->import_count);
	for (uint32_t i = 0; i < count; i++, name += strlen(name) + 1)
	{
		table[i] = 0;
		for (size_t j = 0; j < sizeof(imports) / sizeof(imports[0]); j++)
			if (!strcmp(imports[j].name, name))
				table[i] = (uintptr_t)imports[j].function;
		if (!table[i])
		{
			fprintf(stderr, "unresolved import %s\n", name);
			return -1;
		}
	}
	printf("arena 0x%llx, guest code 0x%x bytes, data at 0x%x, %u imports\n", (unsigned long long)arena,
		MACOS_GUEST_CODE_SIZE, MACOS_GUEST_DATA_ADDRESS, count);
	return 0;
}

static int expect(const char *check, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", check);
	return !passed;
}

struct worker
{
	unsigned count;
	uint32_t callback_result;
};

/* runs on a thread whose stack is in the arena, as the game's threads do */
static void *run_worker(void *argument)
{
	struct worker *worker = argument;

	worker->callback_result = host_guest_invoke(guest_function(header->callback), 1, 2, 3, 4, arena);
	host_guest_invoke(guest_function(header->atomic_add), worker->count, 0, 0, 0, arena);
	return NULL;
}

int main(void)
{
	int failures = 0;
	uint64_t result;
	pthread_t threads[2];
	struct worker workers[2] = {{400000, 0}, {400000, 0}};

	if (load())
		return 1;

	result = host_guest_on_stack(guest_function(header->layout), 0, arena, host_pointer(STACK_BASE + STACK_SIZE));
	printf("layout failure bits: 0x%llx\n", (unsigned long long)result);
	failures += expect("32-bit structures, globals, stack locals, function pointers, jump table", result == 0);

	result = host_guest_on_stack(guest_function(header->imports), 0, arena, host_pointer(STACK_BASE + STACK_SIZE));
	failures += expect("guest calls host imports with guest pointers", result == 0);

	for (unsigned i = 0; i < 2; i++)
	{
		pthread_attr_t attributes;

		pthread_attr_init(&attributes);
		pthread_attr_setstack(&attributes, host_pointer(STACK_BASE + (i + 1) * STACK_SIZE), STACK_SIZE);
		if (pthread_create(&threads[i], &attributes, run_worker, &workers[i]))
		{
			fprintf(stderr, "pthread_create failed\n");
			return 1;
		}
		pthread_attr_destroy(&attributes);
	}
	for (unsigned i = 0; i < 2; i++)
		pthread_join(threads[i], NULL);
	failures += expect("host calls guest with four arguments on arena stacks",
		workers[0].callback_result == 1234 && workers[1].callback_result == 1234);
	result = host_guest_on_stack(guest_function(header->atomic_value), 0, arena, host_pointer(STACK_BASE + STACK_SIZE));
	failures += expect("atomics from two threads", result == 800000);

	printf("%s\n", failures ? "ARENA PROBE FAILED" : "ARENA PROBE OK");
	return failures != 0;
}
