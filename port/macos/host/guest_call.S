/*
GUEST_CALL.S

Calls into guest code. The guest's lifted code (tools/macos_asm_lift.py)
expects x27 to hold the arena's base; the host's own code may use x27 freely,
so each entry saves the host's x27, installs the arena's and restores it.
Guest calls back into the host keep x27, which AAPCS64 makes callee-saved.

Adapted from the iOS port by Nicholas Dominici
(github.com/NicholasDominici/halo-ce-ios, port/ios/host/guest_call.S, CC0 1.0).
*/

	.text
	.p2align 2

/* uint32_t host_guest_invoke(uintptr_t function, uint32_t a, uint32_t b,
uint32_t c, uint32_t d, uintptr_t arena): calls the native address function
with four 32-bit arguments, on the current stack (which must be in the
arena), and returns the guest's w0 */
	.globl _host_guest_invoke
_host_guest_invoke:
	stp x27, x30, [sp, #-16]!
	mov x16, x0
	mov x27, x5
	mov w0, w1
	mov w1, w2
	mov w2, w3
	mov w3, w4
	blr x16
	ldp x27, x30, [sp], #16
	ret

/* uint64_t host_guest_on_stack(uintptr_t function, uint32_t argument,
uintptr_t arena, void *stack_top): switches to the arena stack stack_top,
calls function(argument) and returns its x0 on the original stack */
	.globl _host_guest_on_stack
_host_guest_on_stack:
	stp x27, x30, [sp, #-32]!
	str x29, [sp, #16]
	mov x9, sp
	mov sp, x3
	stp x9, xzr, [sp, #-16]!
	mov x27, x2
	mov x16, x0
	mov w0, w1
	blr x16
	ldr x9, [sp]
	mov sp, x9
	ldr x29, [sp, #16]
	ldp x27, x30, [sp], #32
	ret
