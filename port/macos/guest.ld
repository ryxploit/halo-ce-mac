/*
GUEST.LD

Layout of the macOS guest image (tools/macos_guest_build.py). The addresses
are guest addresses: offsets into the host's 4 GB arena
(docs/macos-port-audit.md). The image is linked at HALO_GUEST_IMAGE_BASE, just
above the Xbox window, as on Android (port/android/include/halo_android_abi.h).

There are exactly two load segments, each aligned to the 16 KB pages of
Apple Silicon: the code, embedded in the app's signed __TEXT and aliased
read/execute into the arena (tools/macos_embed_guest.py), and the writable
data, copied into fresh arena pages at start-up.
*/

ENTRY(__guest_header)

PHDRS
{
	code PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x88000000;
	.guest_header : { KEEP(*(.guest_header)) } :code
	.text : { *(.text .text.*) } :code
	.rodata : { *(.rodata .rodata.*) } :code
	. = ALIGN(0x4000);
	__guest_code_end = .;
	.data : { *(.data .data.*) } :data
	.guest_init : {
		__guest_init_start = .;
		KEEP(*(.guest_init))
		__guest_init_end = .;
	} :data
	.bss : { *(.bss .bss.*) *(COMMON) } :data
	. = ALIGN(0x4000);
	__guest_image_end = .;
	/DISCARD/ : { *(.comment) *(.note.*) *(.guest_fini) *(.eh_frame) }
}
