/*
ARENA_GUEST.C

Guest half of the arena probe (arena_probe.c). It is built exactly as the
game guest is: compiled for arm64_32 with x15 and x27 reserved, converted to
ELF assembly and lifted into the arena (tools/macos_asm_lift.py), linked by
ld.lld with port/macos/guest.ld and embedded in the host
(tools/macos_embed_guest.py). Each function returns 0 on success or a bit
mask of the checks that failed.
*/

struct node
{
	struct node *next;
	unsigned value;
};

_Static_assert(sizeof(struct node) == 8, "guest pointers must stay 32-bit");
_Static_assert(sizeof(long) == 4, "guest long must stay 32-bit");

/* imports from the host (arena_imports.list, tools/android_imports.py) */
unsigned host_probe_sum(const unsigned *values, unsigned count);
void host_probe_write(char *buffer, unsigned size);

static struct node nodes[3];
/* a statically initialised pointer: an absolute 32-bit guest address */
static struct node *list_head = &nodes[0];
static unsigned array[128];
static volatile unsigned shared_counter;

static unsigned __attribute__((noinline)) scale(unsigned x)
{
	return x * 7 + 3;
}

static unsigned (*volatile callback)(unsigned) = scale;

/* external, so that the compiler cannot specialise it for its one caller's
constant; different work in each of many cases, so that it emits a jump table
(an indirect br) rather than a table of values or a tree of compares */
unsigned __attribute__((noinline)) classify(unsigned x, unsigned y);
unsigned __attribute__((noinline)) classify(unsigned x, unsigned y)
{
	switch (x)
	{
	case 0: return y + 11;
	case 1: return y * 13;
	case 2: return y ^ 17;
	case 3: return (y << 3) - 19;
	case 4: return y / 23 + 5;
	case 5: return callback(y) + 29;
	case 6: return y % 31 + 1;
	case 7: return (y >> 1) | 37;
	case 8: return y - 41;
	case 9: return y * y;
	case 10: return y & 43;
	case 11: return ~y;
	default: return 0;
	}
}

static unsigned __attribute__((noinline)) sum_through_pointer(unsigned *const *pointer, unsigned count)
{
	unsigned sum = 0;

	for (unsigned i = 0; i < count; i++)
		sum += (*pointer)[i];
	return sum;
}

unsigned probe_layout(void)
{
	unsigned failures = 0, sum = 0;
	unsigned local[32];
	unsigned *local_pointer = local;

	for (unsigned i = 0; i < 32; i++)
		local[i] = i * 3;
	for (unsigned i = 0; i < 128; i++)
		array[i] = i + 1;
	nodes[0] = (struct node){&nodes[1], 19};
	nodes[1] = (struct node){&nodes[2], 23};
	nodes[2] = (struct node){0, 29};
	for (struct node *node = list_head; node; node = node->next)
		sum += node->value;
	if (sum != 71)
		failures |= 1;
	if (callback(11) != 80)
		failures |= 2;
	/* the address of a stack local, kept in a 32-bit pointer */
	if (sum_through_pointer(&local_pointer, 32) != 1488)
		failures |= 4;
	sum = 0;
	for (unsigned i = 0; i < 128; i++)
		sum += array[i];
	if (sum != 8256)
		failures |= 8;
	sum = 0;
	for (unsigned i = 0; i < 13; i++)
		sum += classify(i, 100);
	/* 111 + 1300 + 117 + 781 + 9 + 732 + 8 + 55 + 59 + 10000 + 32
	+ 0xffffff9b + 0, modulo 2^32 */
	if (sum != 13103u)
		failures |= 16;
	if ((unsigned)list_head < 0x88000000u)
		failures |= 32;
	return failures;
}

unsigned probe_imports(void)
{
	unsigned failures = 0;
	unsigned values[4] = {1, 2, 3, 4};
	char buffer[16];

	if (host_probe_sum(values, 4) != 10)
		failures |= 1;
	host_probe_write(buffer, sizeof(buffer));
	if (buffer[0] != 'm' || buffer[4] != 's' || buffer[5] != '\0')
		failures |= 2;
	return failures;
}

/* the host calls this with four arguments (host_guest_invoke) */
unsigned probe_callback(unsigned a, unsigned b, unsigned c, unsigned d)
{
	return a * 1000 + b * 100 + c * 10 + d;
}

unsigned probe_atomic_add(unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		__atomic_fetch_add(&shared_counter, 1, __ATOMIC_SEQ_CST);
	return 0;
}

unsigned probe_atomic_value(void)
{
	return __atomic_load_n(&shared_counter, __ATOMIC_SEQ_CST);
}

#define PROBE_MAGIC 0x45424f50u /* 'PROB' */

struct probe_header
{
	unsigned magic;
	unsigned (*layout)(void);
	unsigned (*imports)(void);
	unsigned (*callback)(unsigned, unsigned, unsigned, unsigned);
	unsigned (*atomic_add)(unsigned);
	unsigned (*atomic_value)(void);
	unsigned long long *import_table;
	const char *import_names;
	const unsigned *import_count;
};

extern unsigned long long __host_import_table[];
extern const char __host_import_names[];
extern const unsigned __host_import_count;

__attribute__((section("__TEXT,__guest_header"), used))
const struct probe_header __guest_header =
{
	PROBE_MAGIC,
	probe_layout,
	probe_imports,
	probe_callback,
	probe_atomic_add,
	probe_atomic_value,
	__host_import_table,
	__host_import_names,
	&__host_import_count,
};
