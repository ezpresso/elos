#ifndef VFS_SYS_H
#define VFS_SYS_H

#include <arch/syscall.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/signal.h>

struct iovec;
struct dirent;
struct stat64;
struct pollfd;
struct timespec;
struct statfs;

int sys_pipe(int fd[2]);
int sys_pipe2(int fd[2], int flags);

int sys_openat(int at, const char *path, int flags, mode_t mode);
int sys_open(const char *path, int flags, mode_t mode);
int sys_close(int fd);

ssize_t sys_preadv(int fd, struct iovec *iov, int iovc, SYSARG_LL(off));
ssize_t sys_pread64(int fd, void *buf, size_t len, SYSARG_LL(off));
ssize_t sys_readv(int fd, struct iovec *iov, int iovc);
ssize_t sys_read(int fd, void *buf, size_t len);
ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, SYSARG_LL(off));
ssize_t sys_pwrite64(int fd, void *buf, size_t len, SYSARG_LL(off));
ssize_t sys_writev(int fd, struct iovec *iov, int iovc);
ssize_t sys_write(int fd, void *buf, size_t len);

int sys_dup(int fd);
int sys_dup2(int oldfd, int newfd);
int sys_ioctl(int fd, int request, void *argp);
int sys_fcntl64(int fd, int cmd, int arg);
int sys__llseek(int fd, unsigned long off_hi, unsigned long off_lo,
	loff_t *result, int whence);

int sys_getdents64(int fd, struct dirent *dirp, size_t count);

mode_t sys_umask(mode_t mask);
int sys_getcwd(char *ubuf, size_t size);
int sys_chroot(const char *path);
int sys_chdir(const char *path);
int sys_fchdir(int fd);

int sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group,
	int flags);
int sys_chown32(const char *path, uid_t owner, gid_t group);
int sys_lchown32(const char *path, uid_t owner, gid_t group);
int sys_fchown32(int fd, uid_t owner, gid_t group);

int sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int sys_chmod(const char *path, mode_t mode);
int sys_fchmod(int fd, mode_t mode);

int sys_stat64(const char *path, struct stat64 *buf);
int sys_lstat64(const char *path, struct stat64 *buf);
int sys_fstat64(int fd, struct stat64 *ubuf);
int sys_fstatat64(int dirfd, const char *upath, struct stat64 *buf,
	int atflags);

int sys_utimensat(int fd, const char *pathname, const struct timespec *utimes,
	int flags);

int sys_truncate64(const char *path, off_t length);
int sys_ftruncate64(int fd, off_t length);

int sys_mknodat(int at, const char *path, mode_t mode, unsigned dev);
int sys_mknod(const char *path, mode_t mode, unsigned dev);
int sys_mkdirat(int at, const char *path, mode_t mode);
int sys_unlinkat(int dirfd, const char *upath, int flags);
int sys_mkdir(const char *path, mode_t mode);
int sys_unlink(const char *path);
int sys_rmdir(const char *path);
int sys_renameat(int olddirfd, const char *oldpath, int newdirfd,
	const char *newpath);
int sys_rename(const char *oldpath, const char *newpath);
int sys_symlinkat(const char *target, int newdirfd, const char *linkpath);
int sys_symlink(const char *target, __unused const char *linkpath);

int sys_access(const char *path, int mode);
int sys_faccessat(int dirfd, const char *path, int mode, int flags);

ssize_t sys_readlinkat(int dirfd, const char *path, char *buf, size_t size);
ssize_t sys_readlink(const char *path, char *buf, size_t size);

int sys_mount(const char *usource, const char *utarget, const char *ufstype,
	unsigned long flags, const void *data);

int sys_fadvise64_64(int fd, SYSARG_LL(off), SYSARG_LL(len), int advice);

int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int sys_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	const sigset_t *mask, size_t sigsetsz);

int sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	const struct timespec *timeout, const sigset_t *sigmask);
int sys__newselect(int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, const struct timespec *timeout);

int sys_statfs64(const char *path, struct statfs *buf);
int sys_fstatfs64(int fd, struct statfs *buf);

#endif