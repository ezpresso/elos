#ifndef DEVICE_GPU_GPU_H
#define DEVICE_GPU_GPU_H

#include <lib/list.h>

typedef uint64_t gpu_addr_t;
typedef uint64_t gpu_size_t;

typedef struct gpu_device {
	/**
	 * @brief An unique id for the device.
	 */
	int id;

	/**
	 * @brief The id of the devfs entry (/dev/gpuX)y.
	 */
	dev_t file;

	/**
	 * @brief A driver private pointer.
	 */
	void *priv;

	/**
	 * @brief The device driver.
	 */
	struct gpu_driver *driver;

	/**
	 * @brief The node for the global GPU list.
	 */
	list_node_t node;

	/**
	 * @brief A list of the CRTCs of the device.
	 */
	list_t crtc_list;

	/**
	 * @brief A list of the connectors of the device.
	 */
	list_t connector_list;

	/**
	 * @brief A list of the encoders of the device.
	 */
	list_t encoder_list;

#if 0
	struct gpu_memman *mman[GPU_NMEMTYPE];
#endif
} gpu_device_t;


typedef struct gpu_driver {
#define GPU_ACCEL (1 << 0) /* GPU supports graphics acceleration */
	int flags;

	/**
	 * @brief Initialize the graphics hardware.
	 *
	 * Called when the device-file of the gpu is opened the first time.
	 */
	int (*init) (struct gpu_device *device);

	/**
	 * @brief Initialize the graphics hardware.
	 *
	 * Called after every file descriptor of the device-file of the gpu
	 * was closed.
	 */
	int (*uninit) (struct gpu_device *device);
} gpu_driver_t;

void gpu_init(gpu_device_t *gpu, gpu_driver_t *driver, void *priv);
void gpu_uninit(gpu_device_t *gpu);

/**
 * @brief Register a graphics card.
 */
void gpu_register(gpu_device_t *device);

/**
 * @brief Unregister a graphics card.
 * @retval 0		Success
 * @retval -EBUSY	Device cannot be removed because it is still used.
 */
int gpu_unregister(gpu_device_t *device);

static inline int gpu_id(gpu_device_t *device) {
	return device->id;
}

static inline void *gpu_priv(gpu_device_t *device) {
	return device->priv;
}

#endif
