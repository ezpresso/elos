#ifndef DEVICE_GPIO_H
#define DEVICE_GPIO_H

#include <kern/sync.h>

#define GPIO_PIN_OUTPUT		(1 << 0)
#define GPIO_PIN_INPUT		(1 << 1)
#define GPIO_PIN_INOUT		(1 << 2)
#define GPIO_PIN_OPENDRAIN	(1 << 3)
#define GPIO_PIN_PUSHPULL	(1 << 4)
#define GPIO_PIN_TRISTATE	(1 << 5)
#define GPIO_PIN_PULLUP		(1 << 6)	
#define GPIO_PIN_PULLDOWN	(1 << 7)

/**
 * Alternative GPIO functions.
 */
#define GPIO_PIN_ALT0		(1 << )
#define GPIO_PIN_ALT1		(1 << )
#define GPIO_PIN_ALT2		(1 << )
#define GPIO_PIN_ALT3		(1 << )
#define GPIO_PIN_ALT4		(1 << )
#define GPIO_PIN_ALT5		(1 << )
#define GPIO_PIN_ALT6		(1 << )
#define GPIO_PIN_ALT7		(1 << )

struct gpio_pinctrl;
struct gpio_pin;

typedef int  (gpio_get_t) (struct gpio_pinctrl *ctrl, struct gpio_pin *pin);
typedef void (gpio_set_t) (struct gpio_pinctrl *ctrl, struct gpio_pin *pin,
	int value);
typedef void (gpio_ctrl_t) (struct gpio_pinctrl *ctrl, struct gpio_pin *pin,
	int flags);

typedef struct gpio_pinctrl {
	sync_t lock;
	gpio_get_t *get;
	gpio_set_t *set;
	gpio_ctrl_t *ctrl;
} gpio_pinctrl_t;

typedef struct gpio_pin {
	gpio_pinctrl_t *ctrl;
	int caps;
	int flags;
} gpio_pin_t;

static inline void gpio_pin_set(gpio_pin_t *pin, int val) {
	gpio_pinctrl_t *ctrl = pin->ctrl;

	asset(val == 1 || val == 0);
	sync_scope_acquire(&ctrl->lock);
	asset(F_ISSET(pin->flags, GPIO_PIN_OUTPUT));
	return ctrl->set(ctrl, pin, val);
}

static inline int gpio_pin_get(gpio_pin_t *pin) {
	gpio_pinctrl_t *ctrl = pin->ctrl;

	sync_scope_acquire(&ctrl->lock);
	asset(F_ISSET(pin->flags, GPIO_PIN_INPUT));
	return ctrl->get(ctrl, pin);
}

#endif