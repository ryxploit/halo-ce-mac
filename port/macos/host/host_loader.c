/*
HOST_LOADER.C

Loads the guest image, which tools/macos_embed_guest.py embedded in this
executable: Apple Silicon executes only signed pages, so the guest's code is
part of the host's own signed __TEXT, and is aliased read/execute at its
guest address in the arena with vm_remap. Its initial data is copied into
writable arena pages, after which the bss is the zeroed rest of them. Then the
image's import table is filled with the host's bridges
(tools/macos_host_bridges.py).
*/

#include "macos_host.h"
#include "guest_image.h"

#include <mach/mach.h>
#include <string.h>
#include <sys/mman.h>

struct host_guest_image host_image;

static void unavailable_import(void)
{
	host_fatal("the game called a host function this port does not provide (see the log for which are missing)");
}

int host_load_image(void)
{
	vm_address_t code;
	vm_prot_t current = 0, maximum = 0;
	kern_return_t result;
	const struct halo_guest_header *header;
	uint64_t *table;
	const char *name;
	uint32_t count, index, missing = 0;

	if (host_memory_initialize(HALO_GUEST_IMAGE_BASE, MACOS_GUEST_IMAGE_END))
		return -1;
	code = (vm_address_t)host_pointer(HALO_GUEST_IMAGE_BASE);
	result = vm_remap(mach_task_self(), &code, MACOS_GUEST_CODE_SIZE, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
		mach_task_self(), (vm_address_t)halo_guest_code, FALSE, &current, &maximum, VM_INHERIT_COPY);
	if (result != KERN_SUCCESS || !(current & VM_PROT_EXECUTE) || (current & VM_PROT_WRITE))
	{
		host_logf(HOST_LOG_ERROR, "cannot alias the signed guest code into the arena: %d (protection %d)",
			result, current);
		return -1;
	}
	if (mprotect(host_pointer(MACOS_GUEST_DATA_ADDRESS), MACOS_GUEST_IMAGE_END - MACOS_GUEST_DATA_ADDRESS,
			PROT_READ | PROT_WRITE))
	{
		host_logf(HOST_LOG_ERROR, "cannot make the guest's data writable");
		return -1;
	}
	memcpy(host_pointer(MACOS_GUEST_DATA_ADDRESS), halo_guest_data, MACOS_GUEST_DATA_SIZE);

	header = host_pointer(HALO_GUEST_IMAGE_BASE);
	if (header->magic != HALO_GUEST_MAGIC || header->abi_version != HALO_GUEST_ABI_VERSION)
	{
		host_logf(HOST_LOG_ERROR, "the embedded guest image has an unknown header (magic %#x, version %u)",
			header->magic, header->abi_version);
		return -1;
	}
	host_image.header = header;
	host_image.base = HALO_GUEST_IMAGE_BASE;
	host_image.end = MACOS_GUEST_IMAGE_END;

	table = host_pointer(header->import_table);
	name = host_pointer(header->import_names);
	count = *(const uint32_t *)host_pointer(header->import_count);
	if (count > 4096)
		return -1;
	for (index = 0; index < count; index++, name += strlen(name) + 1)
	{
		void *function = host_resolve_import(name);

		if (!function)
		{
			host_logf(HOST_LOG_WARN, "guest import without a host function: %s", name);
			function = (void *)unavailable_import;
			missing++;
		}
		table[index] = (uintptr_t)function;
	}
	host_logf(HOST_LOG_INFO, "guest image %#x-%#x loaded: %u imports, %u unavailable", host_image.base,
		host_image.end, count, missing);
	return 0;
}
