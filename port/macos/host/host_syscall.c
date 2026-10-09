/*
HOST_SYSCALL.C

System calls on behalf of the guest's musl (its syscall_arch.h sends every
call to host_syscall), translated from Linux's AArch64 interface, which musl
speaks, to Darwin's:

- the numbers, flags, commands and errno values are Linux's;
- the guest's pointers are offsets into the arena (host_pointer), and its
  structures have ILP32 layouts with a 32-bit time_t: timespec, iovec, stat,
  linux_dirent64;
- Darwin has no futex: waiters wait on condition variables of their own,
  keyed by guest address;
- memory mappings stay in the arena (host_memory.c);
- the standard output and error streams go to the host's log.

Adapted from the iOS port by Nicholas Dominici
(github.com/NicholasDominici/halo-ce-ios, port/ios/host/host_syscall.c,
CC0 1.0) and port/android/host/host_syscall.c.
*/

#include "macos_host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* the guest's system call numbers (Linux's, __NR_*) */
#include "bits/syscall.h.in"

#define P(type, value) ((type)host_pointer((uint64_t)(value)))

/* Linux's values */
#define LINUX_AT_FDCWD (-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_AT_EACCESS 0x200
#define LINUX_O_CREAT 0100
#define LINUX_O_EXCL 0200
#define LINUX_O_NOCTTY 0400
#define LINUX_O_TRUNC 01000
#define LINUX_O_APPEND 02000
#define LINUX_O_NONBLOCK 04000
#define LINUX_O_DSYNC 010000
#define LINUX_O_DIRECTORY 040000
#define LINUX_O_NOFOLLOW 0100000
#define LINUX_O_CLOEXEC 02000000
#define LINUX_F_DUPFD 0
#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_F_GETFL 3
#define LINUX_F_SETFL 4
#define LINUX_F_DUPFD_CLOEXEC 1030
#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_TIMER_ABSTIME 1
#define LINUX_LOCK_SH 1
#define LINUX_LOCK_EX 2
#define LINUX_LOCK_NB 4
#define LINUX_LOCK_UN 8
#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_WAIT_BITSET 9
#define LINUX_FUTEX_WAKE_BITSET 10
#define LINUX_FUTEX_COMMAND_MASK 0x7f
#define LINUX_FUTEX_CLOCK_REALTIME 0x100
#define LINUX_ENOSYS 38

/* ---------- the guest's structures */

struct guest_timespec
{
	int32_t seconds;
	int32_t nanoseconds;
};

struct guest_iovec
{
	uint32_t base;
	uint32_t length;
};

struct guest_stat
{
	uint64_t device;
	uint64_t inode;
	uint32_t mode;
	uint32_t links;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	int64_t size;
	int32_t block_size;
	int32_t padding;
	int64_t blocks;
	struct guest_timespec access, modification, change;
};

_Static_assert(sizeof(struct guest_stat) == 88, "the guest's struct stat (arm64_32 bits/stat.h)");

struct guest_utsname
{
	char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

/* Linux's ILP32 struct sysinfo; the memory is counted in pages (mem_unit),
so that a Mac's memory fits 32 bits */
struct guest_sysinfo
{
	uint32_t uptime, loads[3], total_ram, free_ram, shared_ram, buffer_ram;
	uint32_t total_swap, free_swap;
	uint16_t processes, padding;
	uint32_t total_high, free_high, memory_unit;
	char reserved[256];
};

/* ---------- errno */

int host_linux_errno(int value)
{
	switch (value)
	{
	case EDEADLK: return 35;
	case EAGAIN: return 11;
	case EINPROGRESS: return 115;
	case EALREADY: return 114;
	case ENOTSOCK: return 88;
	case EDESTADDRREQ: return 89;
	case EMSGSIZE: return 90;
	case EPROTOTYPE: return 91;
	case ENOPROTOOPT: return 92;
	case EPROTONOSUPPORT: return 93;
	case ESOCKTNOSUPPORT: return 94;
	case ENOTSUP: return 95;
	case EOPNOTSUPP: return 95;
	case EPFNOSUPPORT: return 96;
	case EAFNOSUPPORT: return 97;
	case EADDRINUSE: return 98;
	case EADDRNOTAVAIL: return 99;
	case ENETDOWN: return 100;
	case ENETUNREACH: return 101;
	case ENETRESET: return 102;
	case ECONNABORTED: return 103;
	case ECONNRESET: return 104;
	case ENOBUFS: return 105;
	case EISCONN: return 106;
	case ENOTCONN: return 107;
	case ESHUTDOWN: return 108;
	case ETOOMANYREFS: return 109;
	case ETIMEDOUT: return 110;
	case ECONNREFUSED: return 111;
	case ELOOP: return 40;
	case ENAMETOOLONG: return 36;
	case EHOSTDOWN: return 112;
	case EHOSTUNREACH: return 113;
	case ENOTEMPTY: return 39;
	case EUSERS: return 87;
	case EDQUOT: return 122;
	case ESTALE: return 116;
	case ENOLCK: return 37;
	case ENOSYS: return 38;
	case EOVERFLOW: return 75;
	case ECANCELED: return 125;
	case EIDRM: return 43;
	case ENOMSG: return 42;
	case EILSEQ: return 84;
	case EBADMSG: return 74;
	case ENODATA: return 61;
	case ETIME: return 62;
	default:
		/* EPERM to ERANGE (1 to 34) agree, but EDEADLK and EAGAIN above */
		return value;
	}
}

int host_errno(void)
{
	return host_linux_errno(errno);
}

static long long result_of(long long value)
{
	return value == -1 ? -host_linux_errno(errno) : value;
}

static int directory_fd(long long fd)
{
	return (int)fd == LINUX_AT_FDCWD ? AT_FDCWD : (int)fd;
}

static int open_flags(long long linux_flags)
{
	int flags = (int)(linux_flags & 3);

	if (linux_flags & LINUX_O_CREAT)
		flags |= O_CREAT;
	if (linux_flags & LINUX_O_EXCL)
		flags |= O_EXCL;
	if (linux_flags & LINUX_O_NOCTTY)
		flags |= O_NOCTTY;
	if (linux_flags & LINUX_O_TRUNC)
		flags |= O_TRUNC;
	if (linux_flags & LINUX_O_APPEND)
		flags |= O_APPEND;
	if (linux_flags & LINUX_O_NONBLOCK)
		flags |= O_NONBLOCK;
	if (linux_flags & LINUX_O_DSYNC)
		flags |= O_DSYNC;
	if (linux_flags & LINUX_O_DIRECTORY)
		flags |= O_DIRECTORY;
	if (linux_flags & LINUX_O_NOFOLLOW)
		flags |= O_NOFOLLOW;
	if (linux_flags & LINUX_O_CLOEXEC)
		flags |= O_CLOEXEC;
	return flags;
}

static int linux_status_flags(int flags)
{
	return (flags & 3) | ((flags & O_APPEND) ? LINUX_O_APPEND : 0) | ((flags & O_NONBLOCK) ? LINUX_O_NONBLOCK : 0);
}

static clockid_t clock_of(long long id)
{
	switch ((int)id)
	{
	case LINUX_CLOCK_REALTIME: return CLOCK_REALTIME;
	case LINUX_CLOCK_PROCESS_CPUTIME_ID: return CLOCK_PROCESS_CPUTIME_ID;
	case LINUX_CLOCK_THREAD_CPUTIME_ID: return CLOCK_THREAD_CPUTIME_ID;
	case LINUX_CLOCK_MONOTONIC_RAW: return CLOCK_MONOTONIC_RAW;
	default: return CLOCK_MONOTONIC;
	}
}

static int time_in(uint64_t address, struct timespec *value)
{
	const struct guest_timespec *guest = P(const struct guest_timespec *, address);

	if (!guest)
		return 0;
	value->tv_sec = guest->seconds;
	value->tv_nsec = guest->nanoseconds;
	return 1;
}

static void time_out(uint64_t address, const struct timespec *value)
{
	struct guest_timespec *guest = P(struct guest_timespec *, address);

	if (!guest)
		return;
	guest->seconds = (int32_t)value->tv_sec;
	guest->nanoseconds = (int32_t)value->tv_nsec;
}

static void stat_out(uint64_t address, const struct stat *value)
{
	struct guest_stat *guest = P(struct guest_stat *, address);

	memset(guest, 0, sizeof(*guest));
	guest->device = (uint64_t)value->st_dev;
	guest->inode = value->st_ino;
	guest->mode = value->st_mode;
	guest->links = value->st_nlink;
	guest->uid = value->st_uid;
	guest->gid = value->st_gid;
	guest->rdev = (uint64_t)value->st_rdev;
	guest->size = value->st_size;
	guest->block_size = value->st_blksize;
	guest->blocks = value->st_blocks;
	guest->access = (struct guest_timespec){ (int32_t)value->st_atimespec.tv_sec, (int32_t)value->st_atimespec.tv_nsec };
	guest->modification = (struct guest_timespec){ (int32_t)value->st_mtimespec.tv_sec,
		(int32_t)value->st_mtimespec.tv_nsec };
	guest->change = (struct guest_timespec){ (int32_t)value->st_ctimespec.tv_sec, (int32_t)value->st_ctimespec.tv_nsec };
}

/* ---------- standard output and error: the host's log, line by line */

struct log_stream
{
	char line[1024];
	size_t length;
};

static struct log_stream log_streams[2];
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_bytes(int fd, const char *bytes, size_t size)
{
	struct log_stream *stream = &log_streams[fd == 2];
	size_t index;

	pthread_mutex_lock(&log_lock);
	for (index = 0; index < size; index++)
	{
		char c = bytes[index];

		if (c == '\n' || stream->length == sizeof(stream->line) - 1)
		{
			stream->line[stream->length] = 0;
			host_logf(fd == 2 ? HOST_LOG_WARN : HOST_LOG_INFO, "%s", stream->line);
			stream->length = 0;
			if (c == '\n')
				continue;
		}
		stream->line[stream->length++] = c;
	}
	pthread_mutex_unlock(&log_lock);
}

static long long vector_io(long long number, int fd, uint64_t vector, long long count, long long offset)
{
	struct iovec host_vector[64];
	const struct guest_iovec *guest = P(const struct guest_iovec *, vector);
	int index;

	if (count < 0 || count > 64)
		return -22;
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = host_pointer(guest[index].base);
		host_vector[index].iov_len = guest[index].length;
	}
	if ((fd == 1 || fd == 2) && (number == __NR_writev || number == __NR_pwritev))
	{
		long long total = 0;

		for (index = 0; index < count; index++)
		{
			log_bytes(fd, host_vector[index].iov_base, host_vector[index].iov_len);
			total += (long long)host_vector[index].iov_len;
		}
		return total;
	}
	switch (number)
	{
	case __NR_readv: return result_of(readv(fd, host_vector, (int)count));
	case __NR_writev: return result_of(writev(fd, host_vector, (int)count));
	case __NR_preadv: return result_of(preadv(fd, host_vector, (int)count, offset));
	default: return result_of(pwritev(fd, host_vector, (int)count, offset));
	}
}

/* ---------- directories: linux_dirent64 from readdir, per descriptor */

#define DIRECTORY_SLOTS 64

static struct
{
	int fd;
	DIR *stream;
} directories[DIRECTORY_SLOTS];
static pthread_mutex_t directory_lock = PTHREAD_MUTEX_INITIALIZER;

static DIR *directory_stream(int fd, int create)
{
	int index, free_slot = -1;

	for (index = 0; index < DIRECTORY_SLOTS; index++)
	{
		if (directories[index].stream && directories[index].fd == fd)
			return directories[index].stream;
		if (!directories[index].stream && free_slot < 0)
			free_slot = index;
	}
	if (!create || free_slot < 0)
		return NULL;
	{
		int copy = dup(fd);
		DIR *stream = copy >= 0 ? fdopendir(copy) : NULL;

		if (!stream)
		{
			if (copy >= 0)
				close(copy);
			return NULL;
		}
		directories[free_slot].fd = fd;
		directories[free_slot].stream = stream;
		return stream;
	}
}

static void directory_forget(int fd)
{
	int index;

	pthread_mutex_lock(&directory_lock);
	for (index = 0; index < DIRECTORY_SLOTS; index++)
	{
		if (directories[index].stream && directories[index].fd == fd)
		{
			closedir(directories[index].stream);
			directories[index].stream = NULL;
		}
	}
	pthread_mutex_unlock(&directory_lock);
}

static long long guest_getdents64(int fd, uint64_t buffer, uint32_t size)
{
	unsigned char *output = P(unsigned char *, buffer);
	uint32_t used = 0;
	DIR *stream;

	pthread_mutex_lock(&directory_lock);
	stream = directory_stream(fd, 1);
	if (!stream)
	{
		pthread_mutex_unlock(&directory_lock);
		return -9;
	}
	for (;;)
	{
		long position = telldir(stream);
		struct dirent *entry = readdir(stream);
		size_t name_length;
		uint32_t record;

		if (!entry)
			break;
		name_length = strlen(entry->d_name);
		/* d_ino, d_off, d_reclen, d_type, the name and its NUL, 8-aligned */
		record = (uint32_t)((19 + name_length + 1 + 7) & ~(size_t)7);
		if (used + record > size)
		{
			seekdir(stream, position);
			if (!used)
			{
				pthread_mutex_unlock(&directory_lock);
				return -22;
			}
			break;
		}
		memset(output + used, 0, record);
		memcpy(output + used, &entry->d_ino, 8);
		{
			int64_t next = telldir(stream);
			uint16_t length = (uint16_t)record;

			memcpy(output + used + 8, &next, 8);
			memcpy(output + used + 16, &length, 2);
		}
		output[used + 18] = entry->d_type;
		memcpy(output + used + 19, entry->d_name, name_length + 1);
		used += record;
	}
	pthread_mutex_unlock(&directory_lock);
	return used;
}

/* ---------- futex

Each waiter has its own condition variable, so that a wake for one guest
address cannot be taken by a waiter on another. */

struct waiter
{
	uint32_t address;
	int signaled;
	pthread_cond_t condition;
	struct waiter *next;
};

static struct waiter *waiters;
static pthread_mutex_t futex_lock = PTHREAD_MUTEX_INITIALIZER;

static long long guest_futex(uint32_t address, int operation, uint32_t value, uint64_t timeout)
{
	int command = operation & LINUX_FUTEX_COMMAND_MASK;
	struct waiter waiter, **link;
	struct timespec deadline;
	int error = 0;

	pthread_mutex_lock(&futex_lock);
	if (command == LINUX_FUTEX_WAKE || command == LINUX_FUTEX_WAKE_BITSET)
	{
		unsigned woken = 0;
		struct waiter *other;

		for (other = waiters; other && woken < value; other = other->next)
		{
			if (other->address == address && !other->signaled)
			{
				other->signaled = 1;
				pthread_cond_signal(&other->condition);
				woken++;
			}
		}
		pthread_mutex_unlock(&futex_lock);
		return woken;
	}
	if (command != LINUX_FUTEX_WAIT && command != LINUX_FUTEX_WAIT_BITSET)
	{
		pthread_mutex_unlock(&futex_lock);
		return -LINUX_ENOSYS;
	}
	if (__atomic_load_n(P(uint32_t *, address), __ATOMIC_SEQ_CST) != value)
	{
		pthread_mutex_unlock(&futex_lock);
		return -11;
	}
	if (timeout)
	{
		struct timespec delta = {0, 0};

		time_in(timeout, &delta);
		/* FUTEX_WAIT's timeout is relative; FUTEX_WAIT_BITSET's absolute */
		if (command == LINUX_FUTEX_WAIT_BITSET)
		{
			struct timespec now;

			clock_gettime((operation & LINUX_FUTEX_CLOCK_REALTIME) ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
			delta.tv_sec -= now.tv_sec;
			delta.tv_nsec -= now.tv_nsec;
		}
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += delta.tv_sec;
		deadline.tv_nsec += delta.tv_nsec;
		while (deadline.tv_nsec >= 1000000000)
		{
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000;
		}
		while (deadline.tv_nsec < 0)
		{
			deadline.tv_sec--;
			deadline.tv_nsec += 1000000000;
		}
	}
	memset(&waiter, 0, sizeof(waiter));
	waiter.address = address;
	waiter.next = waiters;
	pthread_cond_init(&waiter.condition, NULL);
	waiters = &waiter;
	while (!waiter.signaled && !error)
	{
		error = timeout ? pthread_cond_timedwait(&waiter.condition, &futex_lock, &deadline) :
			pthread_cond_wait(&waiter.condition, &futex_lock);
	}
	for (link = &waiters; *link != &waiter; link = &(*link)->next)
	{
	}
	*link = waiter.next;
	pthread_cond_destroy(&waiter.condition);
	pthread_mutex_unlock(&futex_lock);
	return waiter.signaled ? 0 : -host_linux_errno(error);
}

/* ---------- the rest */

static long long guest_readlinkat(int directory, const char *path, char *buffer, size_t size)
{
	/* the game looks beside its executable for its data (xbox_files.c):
	the executable's folder is the user's folder, the bundle being
	read-only */
	if (!strcmp(path, "/proc/self/exe"))
	{
		char link[1200];
		size_t length;

		snprintf(link, sizeof(link), "%s/halo", host_user_root());
		length = strlen(link);
		if (length > size)
			length = size;
		memcpy(buffer, link, length);
		return (long long)length;
	}
	return result_of(readlinkat(directory, path, buffer, size));
}

static long long guest_uname(uint64_t address)
{
	struct guest_utsname *guest = P(struct guest_utsname *, address);
	struct utsname host;

	if (uname(&host))
		return -host_linux_errno(errno);
	memset(guest, 0, sizeof(*guest));
	snprintf(guest->sysname, sizeof(guest->sysname), "%s", host.sysname);
	snprintf(guest->nodename, sizeof(guest->nodename), "%s", host.nodename);
	snprintf(guest->release, sizeof(guest->release), "%s", host.release);
	snprintf(guest->version, sizeof(guest->version), "%s", host.version);
	snprintf(guest->machine, sizeof(guest->machine), "%s", host.machine);
	return 0;
}

static long long guest_sysinfo(uint64_t address)
{
	struct guest_sysinfo *guest = P(struct guest_sysinfo *, address);
	vm_statistics64_data_t statistics;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	uint64_t memory = 0;
	size_t size = sizeof(memory);
	struct timespec uptime;

	if (sysctlbyname("hw.memsize", &memory, &size, NULL, 0))
		return -host_linux_errno(errno);
	memset(guest, 0, sizeof(*guest));
	clock_gettime(CLOCK_MONOTONIC, &uptime);
	guest->uptime = (uint32_t)uptime.tv_sec;
	guest->memory_unit = (uint32_t)getpagesize();
	guest->total_ram = (uint32_t)(memory / guest->memory_unit);
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&statistics, &count) == KERN_SUCCESS)
		guest->free_ram = (uint32_t)statistics.free_count;
	guest->processes = 1;
	return 0;
}

static long long guest_flock(int fd, int operation)
{
	int host = 0;

	if (operation & LINUX_LOCK_SH)
		host |= LOCK_SH;
	if (operation & LINUX_LOCK_EX)
		host |= LOCK_EX;
	if (operation & LINUX_LOCK_UN)
		host |= LOCK_UN;
	if (operation & LINUX_LOCK_NB)
		host |= LOCK_NB;
	return result_of(flock(fd, host));
}

static long long guest_sleep(long long clock, int absolute, uint64_t request, uint64_t remaining)
{
	struct timespec duration, left = {0, 0};

	if (!time_in(request, &duration))
		return -14;
	if (absolute)
	{
		struct timespec now;

		clock_gettime(clock_of(clock), &now);
		duration.tv_sec -= now.tv_sec;
		duration.tv_nsec -= now.tv_nsec;
		if (duration.tv_nsec < 0)
		{
			duration.tv_sec--;
			duration.tv_nsec += 1000000000;
		}
		if (duration.tv_sec < 0)
			return 0;
	}
	if (nanosleep(&duration, &left))
	{
		if (!absolute)
			time_out(remaining, &left);
		return -host_linux_errno(errno);
	}
	return 0;
}

long long host_syscall(long long number, long long a, long long b, long long c, long long d, long long e, long long f)
{
	switch (number)
	{
	case __NR_read:
		return result_of(read((int)a, P(void *, b), (size_t)(uint32_t)c));
	case __NR_write:
		if (a == 1 || a == 2)
		{
			log_bytes((int)a, P(const char *, b), (size_t)(uint32_t)c);
			return (uint32_t)c;
		}
		return result_of(write((int)a, P(const void *, b), (size_t)(uint32_t)c));
	case __NR_readv:
	case __NR_writev:
	case __NR_preadv:
	case __NR_pwritev:
		return vector_io(number, (int)a, (uint64_t)b, c, d);
	case __NR_openat:
		return result_of(openat(directory_fd(a), P(const char *, b), open_flags(c), (mode_t)d));
	case __NR_close:
		directory_forget((int)a);
		return result_of(close((int)a));
	case __NR_lseek:
	{
		long long position = lseek((int)a, (off_t)b, (int)c);

		/* musl's rewinddir: start the listing again */
		if (position == 0 && b == 0 && c == SEEK_SET)
		{
			DIR *stream;

			pthread_mutex_lock(&directory_lock);
			stream = directory_stream((int)a, 0);
			if (stream)
				rewinddir(stream);
			pthread_mutex_unlock(&directory_lock);
		}
		return result_of(position);
	}
	case __NR_pread64:
		return result_of(pread((int)a, P(void *, b), (size_t)(uint32_t)c, (off_t)d));
	case __NR_pwrite64:
		return result_of(pwrite((int)a, P(const void *, b), (size_t)(uint32_t)c, (off_t)d));
	case __NR_getdents64:
		return guest_getdents64((int)a, (uint64_t)b, (uint32_t)c);
	case __NR_fstat:
	case __NR_newfstatat:
	{
		struct stat value;
		int result = number == __NR_fstat ? fstat((int)a, &value) :
			fstatat(directory_fd(a), P(const char *, b), &value, (d & LINUX_AT_SYMLINK_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0);

		if (!result)
			stat_out(number == __NR_fstat ? (uint64_t)b : (uint64_t)c, &value);
		return result_of(result);
	}
	case __NR_ftruncate:
		return result_of(ftruncate((int)a, (off_t)b));
	case __NR_fsync:
	case __NR_fdatasync:
		return result_of(fsync((int)a));
	case __NR_mkdirat:
		return result_of(mkdirat(directory_fd(a), P(const char *, b), (mode_t)c));
	case __NR_unlinkat:
		return result_of(unlinkat(directory_fd(a), P(const char *, b), (c & LINUX_AT_REMOVEDIR) ? AT_REMOVEDIR : 0));
	case __NR_renameat:
		return result_of(renameat(directory_fd(a), P(const char *, b), directory_fd(c), P(const char *, d)));
	case __NR_renameat2:
		if (e)
			return -22;
		return result_of(renameat(directory_fd(a), P(const char *, b), directory_fd(c), P(const char *, d)));
	case __NR_faccessat:
		return result_of(faccessat(directory_fd(a), P(const char *, b), (int)c, (d & LINUX_AT_EACCESS) ? AT_EACCESS : 0));
	case __NR_readlinkat:
		return guest_readlinkat(directory_fd(a), P(const char *, b), P(char *, c), (size_t)(uint32_t)d);
	case __NR_getcwd:
		if (!getcwd(P(char *, a), (size_t)(uint32_t)b))
			return -host_linux_errno(errno);
		return (long long)strlen(P(char *, a)) + 1;
	case __NR_chdir:
		return result_of(chdir(P(const char *, a)));
	case __NR_fchmod:
		return result_of(fchmod((int)a, (mode_t)b));
	case __NR_fchmodat:
		return result_of(fchmodat(directory_fd(a), P(const char *, b), (mode_t)c, 0));
	case __NR_utimensat:
	{
		struct timespec times[2];
		int has_times = b ? time_in((uint64_t)c, &times[0]) : 0;

		if (has_times)
			time_in((uint64_t)c + sizeof(struct guest_timespec), &times[1]);
		return result_of(utimensat(directory_fd(a), P(const char *, b), has_times ? times : NULL,
			(d & LINUX_AT_SYMLINK_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0));
	}
	case __NR_umask:
		return umask((mode_t)a);
	case __NR_fcntl:
		switch ((int)b)
		{
		case LINUX_F_DUPFD: return result_of(fcntl((int)a, F_DUPFD, (int)c));
		case LINUX_F_DUPFD_CLOEXEC: return result_of(fcntl((int)a, F_DUPFD_CLOEXEC, (int)c));
		case LINUX_F_GETFD: return result_of(fcntl((int)a, F_GETFD));
		case LINUX_F_SETFD: return result_of(fcntl((int)a, F_SETFD, (int)c));
		case LINUX_F_GETFL:
		{
			int flags = fcntl((int)a, F_GETFL);

			return flags < 0 ? result_of(flags) : linux_status_flags(flags);
		}
		case LINUX_F_SETFL: return result_of(fcntl((int)a, F_SETFL, open_flags(c) & (O_APPEND | O_NONBLOCK)));
		default: return -22;
		}
	case __NR_dup:
		return result_of(dup((int)a));
	case __NR_dup3:
	{
		int result = dup2((int)a, (int)b);

		if (result >= 0 && (c & LINUX_O_CLOEXEC))
			fcntl(result, F_SETFD, FD_CLOEXEC);
		return result_of(result);
	}
	case __NR_pipe2:
	{
		int ends[2];
		int *guest = P(int *, a);

		if (pipe(ends))
			return -host_linux_errno(errno);
		if (b & LINUX_O_CLOEXEC)
		{
			fcntl(ends[0], F_SETFD, FD_CLOEXEC);
			fcntl(ends[1], F_SETFD, FD_CLOEXEC);
		}
		if (b & LINUX_O_NONBLOCK)
		{
			fcntl(ends[0], F_SETFL, O_NONBLOCK);
			fcntl(ends[1], F_SETFL, O_NONBLOCK);
		}
		guest[0] = ends[0];
		guest[1] = ends[1];
		return 0;
	}
	case __NR_flock:
		return guest_flock((int)a, (int)b);
	case __NR_ioctl:
		return -25;

	case __NR_clock_gettime:
	case __NR_clock_getres:
	{
		struct timespec value;
		int result = number == __NR_clock_gettime ? clock_gettime(clock_of(a), &value) : clock_getres(clock_of(a), &value);

		if (!result)
			time_out((uint64_t)b, &value);
		return result_of(result);
	}
	case __NR_gettimeofday:
	{
		struct timeval value;
		struct guest_timespec *guest = P(struct guest_timespec *, a);

		gettimeofday(&value, NULL);
		if (guest)
		{
			guest->seconds = (int32_t)value.tv_sec;
			guest->nanoseconds = (int32_t)value.tv_usec;
		}
		return 0;
	}
	case __NR_nanosleep:
		return guest_sleep(LINUX_CLOCK_MONOTONIC, 0, (uint64_t)a, (uint64_t)b);
	case __NR_clock_nanosleep:
		return guest_sleep(a, (b & LINUX_TIMER_ABSTIME) != 0, (uint64_t)c, (uint64_t)d);
	case __NR_futex:
		return guest_futex((uint32_t)a, (int)b, (uint32_t)c, (uint64_t)d);
	case __NR_ppoll:
	{
		struct timespec timeout;
		int has_timeout = time_in((uint64_t)c, &timeout);

		return result_of(poll(P(struct pollfd *, a), (nfds_t)(uint32_t)b,
			has_timeout ? (int)(timeout.tv_sec * 1000 + timeout.tv_nsec / 1000000) : -1));
	}
	case __NR_sched_yield:
		return result_of(sched_yield());

	case __NR_mmap:
		return host_guest_mmap((uint64_t)a, (uint64_t)b, (int)c, (int)d, (int)e, f);
	case __NR_munmap:
		return host_guest_munmap((uint64_t)a, (uint64_t)b);
	case __NR_mprotect:
		return host_guest_mprotect((uint64_t)a, (uint64_t)b, (int)c);
	case __NR_madvise:
		return 0;
	case __NR_mremap:
	case __NR_brk:
		/* musl then falls back to mmap and copying */
		return -12;

	case __NR_getpid:
		return getpid();
	case __NR_getppid:
		return getppid();
	case __NR_gettid:
	case __NR_set_tid_address:
	{
		uint64_t thread = 0;

		pthread_threadid_np(NULL, &thread);
		return (uint32_t)thread;
	}
	case __NR_getuid:
	case __NR_geteuid:
		return getuid();
	case __NR_getgid:
	case __NR_getegid:
		return getgid();
	case __NR_getrandom:
		arc4random_buf(P(void *, a), (size_t)(uint32_t)b);
		return (uint32_t)b;
	case __NR_uname:
		return guest_uname((uint64_t)a);
	case __NR_sysinfo:
		return guest_sysinfo((uint64_t)a);
	case __NR_prlimit64:
	{
		struct rlimit value;

		if (a != 0 || !d)
			return -22;
		/* (the limits' numbering differs; the game only asks) */
		value.rlim_cur = value.rlim_max = RLIM_INFINITY;
		memcpy(P(void *, d), &value, sizeof(value));
		return 0;
	}
	case __NR_kill:
	case __NR_tkill:
	case __NR_tgkill:
		/* (musl's raise and abort) */
		host_fatal("the game raised signal %lld", number == __NR_tgkill ? c : b);
	case __NR_exit:
	case __NR_exit_group:
		host_exit((int)a);
	case __NR_rt_sigaction:
	case __NR_rt_sigprocmask:
	case __NR_sigaltstack:
	case __NR_membarrier:
		/* the host owns signal handling */
		return 0;
	default:
		host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
		return -LINUX_ENOSYS;
	}
}
