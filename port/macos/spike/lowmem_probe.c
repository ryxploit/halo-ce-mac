/*
LOWMEM_PROBE.C

Feasibility probe for the ILP32 guest design (docs/macos-port-audit.md).
The engine's guest image must live below 4 GiB at fixed addresses such as
0x80000000 (PLATFORM_CONTIGUOUS_BASE) and 0x88000000 (the Android guest
origin). This program tries to map those addresses from a normal 64-bit
arm64 process, before and after releasing the executable's __PAGEZERO.

Build: ninja macos_lowmem_probe. Run: build/macos/lowmem_probe.
*/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static void probe_fixed(unsigned long address)
{
	void *mapped = mmap((void *)address, 0x1000, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);

	if (mapped == MAP_FAILED)
		printf("MAP_FIXED 0x%08lx: %s\n", address, strerror(errno));
	else
	{
		printf("MAP_FIXED 0x%08lx: mapped at %p\n", address, mapped);
		munmap(mapped, 0x1000);
	}
}

static void probe_hint(void)
{
	void *mapped = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	printf("unhinted mmap: %p\n", mapped);
	if (mapped != MAP_FAILED)
		munmap(mapped, 0x1000);
}

int main(void)
{
	static const unsigned long addresses[] = {
		0x10000000UL, 0x80000000UL, 0x88000000UL, 0xf0000000UL, 0x100000000UL,
	};
	size_t i;

	printf("sizeof(void *) = %zu\n", sizeof(void *));
	printf("-- with the default __PAGEZERO (4 GiB)\n");
	for (i = 0; i < sizeof addresses / sizeof addresses[0]; i++)
		probe_fixed(addresses[i]);
	probe_hint();

	printf("-- after munmap of [0, 4 GiB)\n");
	if (munmap(NULL, 0x100000000UL) != 0)
		printf("munmap: %s\n", strerror(errno));
	for (i = 0; i < sizeof addresses / sizeof addresses[0]; i++)
		probe_fixed(addresses[i]);
	probe_hint();
	return 0;
}
