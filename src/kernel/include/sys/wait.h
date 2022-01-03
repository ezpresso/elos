#ifndef USR_SYS_WAIT_H
#define USR_SYS_WAIT_H

#define WNOHANG		1
#define WUNTRACED	2

#define WSTOPPED	2
#define WEXITED		4
#define WCONTINUED	8
#define WNOWAIT		0x1000000

/* core is 1 or 0 */
#define W_EXITCODE(sta, sig, core) ((((sta)&0xff)<<8) | (sig&0x7f) | (core<<7))
#define	W_STOPCODE(sig) ((sig) << 8 | 0x7f)
#define W_CONTCODE 0xFFFF /* process was resumed by delivery of SIGCONT */

#define WIFSTOPPED(s) ((short)((((s)&0xffff)*0x10001)>>8) > 0x7f00)

#endif