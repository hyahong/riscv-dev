#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

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

