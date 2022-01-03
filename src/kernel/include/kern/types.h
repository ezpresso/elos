#ifndef KERN_TPYES_H
#define KERN_TPYES_H

#define __NEED_UID_T
#define __NEED_GID_T
#define __NEED_PID_T
#define __NEED_SIZE_T
#define __NEED_SSIZE_T
#define __NEED_OFF_T
#define __NEED_DEV_T
#define __NEED_INO_T
#define __NEED_MODE_T
#define __NEED_NLINK_T
#define __NEED_BLKCNT_T
#define __NEED_BLKSIZE_T
#include <sys/alltypes.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/stddef.h>
#include <arch/types.h>

typedef enum vm_seg {
	KERNELSPACE = 0,
	USERSPACE = 1,
} vm_seg_t;

/**
 * Represents the offset (of a mapping or a page) inside a virtual memory
 * object. This value is usually page aligned. The reason for the length
 * of 64-bits is that the objects of vnodes may be that large.
 */
#define VM_OBJOFF_MAX UINT64_MAX
typedef uint64_t vm_objoff_t;

typedef off_t loff_t;

typedef uint64_t blkno_t; /* block number */

typedef uint64_t frequency_t; /* in HZ */
typedef uint64_t nanosec_t;

/* Each cpu has an id. */
#define CPU_ID_ANY UINT8_MAX
typedef uint8_t cpu_id_t;

#endif
