/*
HVF_PROBE.C

Feasibility probe for running the ILP32 game guest on macOS. A normal macOS
arm64 process cannot map the low 4 GiB that the guest needs (lowmem_probe.c).
A Hypervisor.framework VM can: the guest's addresses are its own. This program
creates a VM, maps 64 MiB of host memory at guest address 0x80000000, builds
an identity page table, and calls the arm64_32 functions of hvf_guest.c.

It tests 32-bit pointers, calls from the guest to the host (hvc), the FPU, the
cost of an exit to the host, shared memory and atomics between two vCPUs on
two host threads, and a guest page fault reported to the host.

Build: ninja macos_hvf_probe (signs the binary with the hypervisor
entitlement). Run: build/macos/hvf_probe. The exit status is 0 when every test
passes.
*/

#include <Hypervisor/Hypervisor.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "hvf_guest_blob.h"

#define GUEST_BASE 0x80000000ULL
#define GUEST_SIZE (64ULL << 20)
#define PAGE_TABLE_OFFSET 0x0000
#define VECTOR_OFFSET 0x1000
#define CODE_OFFSET 0x2000
#define RETURN_OFFSET 0x3000
#define DATA_OFFSET 0x10000
#define STACK_TOP (GUEST_SIZE - 0x1000)
#define STACK_SIZE 0x100000
#define UNMAPPED_GUEST_ADDRESS 0x10000000u

#define HVC(immediate) (0xD4000002u | ((uint32_t)(immediate) << 5))
#define EXCEPTION_CLASS_HVC64 0x16
#define EXCEPTION_CLASS_DATA_ABORT_LOWER 0x24
#define GUEST_FAULTED UINT64_MAX

#define CHECK(call) check((call), #call)

static uint8_t *guest_memory;
static uint64_t last_fault_address;

static void check(hv_return_t result, const char *call)
{
	if (result != HV_SUCCESS)
	{
		fprintf(stderr, "%s failed: 0x%x\n", call, (unsigned)result);
		if (result == HV_DENIED)
			fprintf(stderr, "the binary needs the com.apple.security.hypervisor entitlement\n");
		exit(1);
	}
}

static void *guest_to_host(uint64_t guest_address)
{
	return guest_memory + (guest_address - GUEST_BASE);
}

static void setup_vm(void)
{
	uint64_t *level1;

	guest_memory = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (guest_memory == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}
	CHECK(hv_vm_create(NULL));
	CHECK(hv_vm_map(guest_memory, GUEST_BASE, GUEST_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));

	/* 4 KiB granule, T0SZ = 32: a level-1 table of four 1 GiB blocks maps
	the 32-bit address space to the same IPAs; only 0x80000000 is backed */
	level1 = guest_to_host(GUEST_BASE + PAGE_TABLE_OFFSET);
	for (uint64_t i = 0; i < 4; i++)
		level1[i] = (i << 30) | (1ULL << 10) /* AF */ | (3ULL << 8) /* inner shareable */ | 1 /* block, attr 0 */;
	/* each exception vector exits to the host with hvc #(0x100 + vector) */
	for (unsigned i = 0; i < 16; i++)
		*(uint32_t *)guest_to_host(GUEST_BASE + VECTOR_OFFSET + i * 0x80) = HVC(0x100 + i);
	memcpy(guest_to_host(GUEST_BASE + CODE_OFFSET), hvf_guest, sizeof(hvf_guest));
	*(uint32_t *)guest_to_host(GUEST_BASE + RETURN_OFFSET) = HVC(0);
}

static hv_vcpu_t create_vcpu(hv_vcpu_exit_t **exit_info, uint64_t stack_top)
{
	hv_vcpu_t vcpu;

	CHECK(hv_vcpu_create(&vcpu, exit_info, NULL));
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, 0xFF)); /* attr 0: normal write-back */
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1,
		32ULL /* T0SZ */ | (1ULL << 8) | (1ULL << 10) /* write-back walks */ | (3ULL << 12) /* inner shareable */ |
		(1ULL << 23) /* no TTBR1 */ | (1ULL << 32) /* 36-bit IPA */));
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, GUEST_BASE + PAGE_TABLE_OFFSET));
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, GUEST_BASE + VECTOR_OFFSET));
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, 3ULL << 20)); /* FP and SIMD */
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, 0x30D00800ULL | 1 /* MMU */ | 4 /* D-cache */ | 0x1000 /* I-cache */));
	CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, stack_top));
	return vcpu;
}

/* calls a guest function with two integer arguments; returns its x0, or
GUEST_FAULTED with last_fault_address set */
static uint64_t call_guest(hv_vcpu_t vcpu, hv_vcpu_exit_t *exit_info, uint32_t function,
	uint64_t argument0, uint64_t argument1, unsigned long *host_calls)
{
	CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, GUEST_BASE + CODE_OFFSET + function));
	CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3C5)); /* EL1h, interrupts masked */
	CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X0, argument0));
	CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X1, argument1));
	CHECK(hv_vcpu_set_reg(vcpu, HV_REG_LR, GUEST_BASE + RETURN_OFFSET));
	for (;;)
	{
		uint64_t syndrome, x0;
		unsigned exception_class, immediate;

		CHECK(hv_vcpu_run(vcpu));
		if (exit_info->reason != HV_EXIT_REASON_EXCEPTION)
		{
			fprintf(stderr, "unexpected VM exit reason %u\n", (unsigned)exit_info->reason);
			exit(1);
		}
		syndrome = exit_info->exception.syndrome;
		exception_class = (unsigned)(syndrome >> 26);
		immediate = (unsigned)(syndrome & 0xFFFF);
		if (exception_class == EXCEPTION_CLASS_DATA_ABORT_LOWER)
		{
			/* stage-2 fault: the guest touched an IPA without host memory */
			last_fault_address = exit_info->exception.virtual_address;
			return GUEST_FAULTED;
		}
		if (exception_class != EXCEPTION_CLASS_HVC64)
		{
			fprintf(stderr, "unexpected guest exception: ESR 0x%llx\n", (unsigned long long)syndrome);
			exit(1);
		}
		if (immediate >= 0x100)
		{
			/* a guest vector: a fault inside the guest's own page tables */
			CHECK(hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_FAR_EL1, &last_fault_address));
			return GUEST_FAULTED;
		}
		if (host_calls)
			++*host_calls;
		CHECK(hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0));
		if (immediate == 0)
			return x0;
		if (immediate == 1)
			CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X0, (uint32_t)((int32_t)x0 * (int32_t)x0)));
	}
}

static double seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

struct atomic_worker
{
	unsigned index;
	int count;
};

static void *run_atomic_worker(void *argument)
{
	struct atomic_worker *worker = argument;
	hv_vcpu_exit_t *exit_info;
	hv_vcpu_t vcpu = create_vcpu(&exit_info, GUEST_BASE + STACK_TOP - (worker->index + 1) * STACK_SIZE);

	call_guest(vcpu, exit_info, HVF_GUEST_ATOMIC_ADD, GUEST_BASE + DATA_OFFSET + 0x1000, (uint64_t)worker->count, NULL);
	hv_vcpu_destroy(vcpu);
	return NULL;
}

static int expect(const char *test, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", test);
	return !passed;
}

int main(void)
{
	hv_vcpu_exit_t *exit_info;
	hv_vcpu_t vcpu;
	int failures = 0;
	uint32_t *nodes;
	uint64_t result;

	setup_vm();
	vcpu = create_vcpu(&exit_info, GUEST_BASE + STACK_TOP);

	result = call_guest(vcpu, exit_info, HVF_GUEST_TYPE_SIZES, 0, 0, NULL);
	printf("guest type sizes: %llu\n", (unsigned long long)result);
	failures += expect("ILP32 types (4-byte pointer and long, 8-byte node)", result == 448);

	/* a list of 100 nodes with 32-bit next pointers */
	nodes = guest_to_host(GUEST_BASE + DATA_OFFSET);
	for (unsigned i = 0; i < 100; i++)
	{
		nodes[i * 2] = i < 99 ? (uint32_t)(GUEST_BASE + DATA_OFFSET + (i + 1) * 8) : 0;
		nodes[i * 2 + 1] = i + 1;
	}
	result = call_guest(vcpu, exit_info, HVF_GUEST_SUM_LIST, GUEST_BASE + DATA_OFFSET, 0, NULL);
	failures += expect("32-bit pointers at 0x80010000", result == 5050);

	result = call_guest(vcpu, exit_info, HVF_GUEST_CALL_HOST, 12, 0, NULL);
	failures += expect("guest calls the host with hvc", result == 145);

	{
		double a = 1.5, b = 4.25, product;
		hv_simd_fp_uchar16_t q0 = {0}, q1 = {0};

		memcpy(&q0, &a, sizeof(a));
		memcpy(&q1, &b, sizeof(b));
		CHECK(hv_vcpu_set_simd_fp_reg(vcpu, HV_SIMD_FP_REG_Q0, q0));
		CHECK(hv_vcpu_set_simd_fp_reg(vcpu, HV_SIMD_FP_REG_Q1, q1));
		call_guest(vcpu, exit_info, HVF_GUEST_MULTIPLY, 0, 0, NULL);
		CHECK(hv_vcpu_get_simd_fp_reg(vcpu, HV_SIMD_FP_REG_Q0, &q0));
		memcpy(&product, &q0, sizeof(product));
		failures += expect("FPU in the guest", product == 6.375);
	}

	{
		const int count = 200000;
		unsigned long host_calls = 0;
		double start = seconds(), elapsed;

		call_guest(vcpu, exit_info, HVF_GUEST_SPIN, (uint64_t)count, 0, &host_calls);
		elapsed = seconds() - start;
		printf("host call cost: %lu calls in %.3f s, %.2f us each\n", host_calls, elapsed, elapsed * 1e6 / (double)host_calls);
		failures += expect("host call count", host_calls == (unsigned long)count + 1);
	}

	{
		pthread_t threads[2];
		struct atomic_worker workers[2] = {{1, 500000}, {2, 500000}};
		volatile int *counter = guest_to_host(GUEST_BASE + DATA_OFFSET + 0x1000);

		*counter = 0;
		for (unsigned i = 0; i < 2; i++)
			pthread_create(&threads[i], NULL, run_atomic_worker, &workers[i]);
		for (unsigned i = 0; i < 2; i++)
			pthread_join(threads[i], NULL);
		failures += expect("atomics between two vCPUs on two threads", *counter == 1000000);
	}

	result = call_guest(vcpu, exit_info, HVF_GUEST_LOAD, UNMAPPED_GUEST_ADDRESS, 0, NULL);
	printf("guest fault address: 0x%llx\n", (unsigned long long)last_fault_address);
	failures += expect("guest page fault reaches the host",
		result == GUEST_FAULTED && last_fault_address == UNMAPPED_GUEST_ADDRESS);

	hv_vcpu_destroy(vcpu);
	CHECK(hv_vm_unmap(GUEST_BASE, GUEST_SIZE));
	CHECK(hv_vm_destroy());
	munmap(guest_memory, GUEST_SIZE);
	printf("%s\n", failures ? "HVF PROBE FAILED" : "HVF PROBE OK");
	return failures != 0;
}
