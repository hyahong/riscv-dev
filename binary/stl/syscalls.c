#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <printk.h>

#undef errno
extern int errno;

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
