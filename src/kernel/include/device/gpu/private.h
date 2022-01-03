#ifndef DEVICE_GPU_PRIVATE
#define DEVICE_GPU_PRIVATE

#include <lib/list.h>
#include <kern/sync.h>

struct gpu_device;

extern list_t gpu_list;
extern sync_t gpu_lock;

int gpu_file_create(struct gpu_device *device);
void gpu_file_destroy(struct gpu_device *device);

#endif
