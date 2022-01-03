#include <kern/system.h>
#include <net/socket.h>
#include <sys/netinet/in.h>
#include <sys/socket.h>

int sys_socket(int domain, int type, int protocol) {
	(void) domain;
	(void) type;
	(void) protocol;
	return 0;
}

int sys_connect(__unused int fd, __unused const struct sockaddr *addr, __unused socklen_t len) {
	return -ENOSYS;

#if 0
	file_t *file;

	file = file_get(fd);
	if(!file) {
		return -EBADF;
	}

	if(file->type != FILE_SOCK) {
		file_put(file);
		return -ENOTSOCK;
	}

	file_put(file);

	kpanic("sys_connect\n");

	(void)addr;
	(void)len;

	return 0;
#endif
}

int sys_socketcall(int call, unsigned long *args) {
	switch(call) {
	case __SC_socket:
		return sys_socket(args[0], args[1], args[2]);
	case __SC_connect:
		return sys_connect(args[0], (void *)args[1], args[2]);
	default:
		kprintf("[socket] warning: socketcall: %d", call);
	}

	return -ENOSYS;
}
