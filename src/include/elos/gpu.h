#ifndef __ELOS_GPU_H__
#define __ELOS_GPU_H__

#include <sys/ioctl.h>

#define GPU_CTRL_FILE "gpuctrl"
#define GPU_IOCTL_MAGIC 'G'

/* typedef uint32_t gpu_id_t; */

struct gpu_ioctl_info {
	char driver[32]; /**< GPU userspace driver name. */
	uint32_t id;

#define GPU_CLASS_FB		0 /* Framebuffer only (i.e. no acceleration). */
#define GPU_CLASS_INTEGRATED	1 /* Integrated graphics card. */
#define GPU_CLASS_DEDICATED	2 /* Dedicated graphics card. */
	uint32_t class;
};

#define GPU_IOCTL_TOPO _IOWR(GPU_IOCTL_MAGIC, 0, struct gpu_ioctl_topo)
struct gpu_ioctl_topo {
	uint64_t gpu_count;
	uint64_t info_ptr;
};

#endif
