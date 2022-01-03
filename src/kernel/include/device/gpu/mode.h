#ifndef DEVICE_GPU_MODE_H
#define DEVICE_GPU_MODE_H

struct gpu_device;

typedef struct gpu_display_mode {
	int clock; /* in kHz */
	int hdisplay;
	int hsync_start;
	int hsync_end;
	int htotal;
	int hskew;
	int vdisplay;
	int vsync_start;
	int vsync_end;
	int vtotal;
	int vscan;
} gpu_display_mode_t;

typedef struct gpu_fb {

} gpu_fb_t;

typedef struct gpu_crtc_state {
} gpu_crtc_state_t;

typedef struct gpu_crtc_ops {
} gpu_crtc_ops_t;

typedef struct gpu_crtc {
	struct gpu_device *device;
	list_node_t node;
	int id;
	gpu_crtc_ops_t *ops;
} gpu_crtc_t;

#define GPU_CONNECTOR_MAX_ENCODER 3
typedef struct gpu_connector_state {
	//gpu_crtc_t *crtc; /*< The CRTC of the connector, NULL if disabled. */
} gpu_connector_state_t;

typedef struct gpu_connector_ops {
} gpu_connector_ops_t;

typedef enum gpu_connector_type {
	GPU_CONNECTOR_VIRT,
} gpu_connector_type_t;

typedef struct gpu_connector {
	struct gpu_device *device;
	list_node_t node;
	int id;
	gpu_connector_ops_t *ops;
	gpu_connector_type_t type;
	struct gpu_encoder *encoders[GPU_CONNECTOR_MAX_ENCODER];
	gpu_connector_state_t *state;
	gpu_connector_state_t *newstate;
} gpu_connector_t;

typedef struct gpu_encoder_state {
} gpu_encoder_state_t;

typedef struct gpu_encoder_ops {
} gpu_encoder_ops_t;

typedef enum gpu_encoder_type {
	GPU_ENCODER_DAC,
} gpu_encoder_type_t;

typedef struct gpu_encoder {
	struct gpu_device *device;
	list_node_t node;
	int id;
	gpu_encoder_ops_t *ops;
	gpu_encoder_type_t type;
	uint32_t crtc_mask; /**< Mask of crtcs which can be used. */
	gpu_encoder_state_t *state;
	gpu_encoder_state_t *newstate;
} gpu_encoder_t;

typedef struct gpu_plane_state {
	gpu_fb_t *fb;
} gpu_plane_state_t;

typedef struct gpu_plane {
	gpu_plane_state_t *state;
	gpu_plane_state_t *newstate;
} gpu_plane_t;

void gpu_crtc_init(struct gpu_device *device, gpu_crtc_ops_t *ops,
	gpu_crtc_t *crtc);
void gpu_encoder_init(struct gpu_device *device, gpu_encoder_type_t type,
	gpu_encoder_ops_t *ops, gpu_encoder_t *encoder);
void gpu_connector_init(struct gpu_device *device, gpu_connector_type_t type,
	gpu_connector_ops_t *ops, gpu_connector_t *connector);
void gpu_connector_add_encoder(gpu_connector_t *connector, gpu_encoder_t *enc);
void gpu_encoder_add_crtc(gpu_encoder_t *encoder, gpu_crtc_t *crtc);

#endif
