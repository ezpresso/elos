#ifndef DEVICE_GPU_MM_H
#define DEVICE_GPU_MM_H

/* TODO Non-coherent memory */
typedef enum gpu_memtype {
	GPU_MEM_SYSRAM = 0, /**< Memory inside the system RAM (not accessible by the GPU) */
	GPU_MEM_VRAM = 1, /**< Memory inside the GPU video RAM */
	GPU_MEM_GART = 2,
	GPU_NMEMTYPE = 3,
} gpu_memtype_t;

typedef struct gpu_mem {
} gpu_mem_t;

#if 0
#define GPU_MEM_CONTIG
#define GPU_MEM_UC
gpu_mem_t *gpu_mem_alloc(gpu_device_t *device, gpu_memtype_t type,
	gpu_size_t size, gpu_size_t align, int flags);
void gpu_mem_free(gpu_device_t *device);
//int gpu_mmap();
#endif

#endif
