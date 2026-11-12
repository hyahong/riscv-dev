#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <printk.h>

#undef errno
extern int errno;

ssize_t
_write (int fd, const void *buf, size_t count)
{
	(void) fd;
	(void) buf;
	(void) count;

	errno = ENOSYS;
	return (ssize_t) -1;
}

ssize_t
_read (int fd, void *buf, size_t count)
{
	(void) fd;
	(void) buf;
	(void) count;

	errno = ENOSYS;
	return (ssize_t) -1;
}

int
_close (int fd)
{
	(void) fd;

	errno = ENOSYS;
	return -1;
}

off_t
_lseek (int fd, off_t offset, int whence)
{
	(void) fd;
	(void) offset;
	(void) whence;

	errno = ENOSYS;
	return (off_t) -1;
}

void
_exit (int status)
{
	printk ("exit: status=%d\n", status);

	for (;;)
		__asm__ volatile ("wfi");
}

int
_kill (pid_t pid, int sig)
{
	(void) pid;
	(void) sig;

	errno = ENOSYS;
	return -1;
}

pid_t
_getpid (void)
{
	return 1;
}

int
_fstat (int fd, struct stat *st)
{
	(void) fd;
	(void) st;

	errno = ENOSYS;
	return -1;
}

int
_isatty (int fd)
{
	(void) fd;

	errno = ENOSYS;
	return 0;
}

