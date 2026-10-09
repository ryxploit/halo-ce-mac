/*
HVF_GUEST.C

ILP32 guest code for the hypervisor probe (hvf_probe.c). It is compiled for
arm64_32, the guest ABI of the Android port, and runs inside a
Hypervisor.framework VM at the Xbox address 0x80000000. Each function tests one
thing that the game guest needs. hvc #1 and hvc #2 are calls to the host; the
host returns to the guest after it handles them.
*/

struct node
{
	struct node *next;
	int value;
};

static inline int host_square(int value)
{
	register int x0 __asm__("w0") = value;

	__asm__ volatile("hvc #1" : "+r"(x0) : : "memory");
	return x0;
}

static inline void host_nop(void)
{
	__asm__ volatile("hvc #2" : : : "memory");
}

/* 100 * pointer + 10 * long + node: 448 for ILP32 */
int type_sizes(void)
{
	return (int)sizeof(void *) * 100 + (int)sizeof(long) * 10 + (int)sizeof(struct node);
}

int sum_list(struct node *node)
{
	int sum = 0;

	for (; node; node = node->next)
		sum += node->value;
	return sum;
}

int call_host(int value)
{
	return host_square(value) + 1;
}

double multiply(double a, double b)
{
	return a * b;
}

int spin(int count)
{
	for (int i = 0; i < count; i++)
		host_nop();
	return count;
}

int atomic_add(volatile int *counter, int count)
{
	for (int i = 0; i < count; i++)
		__atomic_fetch_add(counter, 1, __ATOMIC_SEQ_CST);
	return count;
}

int load(volatile int *address)
{
	return *address;
}
