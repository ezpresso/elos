#ifndef KERN_LOG_H
#define KERN_LOG_H

typedef enum log_color {
	LOG_RED,
	LOG_YELLOW,
	LOG_GREY,
	LOG_WHITE,
} log_color_t;

#define LOG_NORMAL LOG_WHITE

#define indprintf(ind, fmt...) ({		\
		for(int i = 0; i < (ind); i++)	\
			kprintf(" ");		\
		kprintf(fmt);			\
	})

void log_reset(void);
void log_screen_disable(void);
void log_screen_enable(void);
bool log_screen_enabled(void);

/* defined by architecture */
int log_width(void);
int log_height(void);

void log_on_put(char c);
void log_putchar(int x, int y, log_color_t color, char c);
void log_set_cursor(int x, int y);
void log_scroll(void);
void log_clear_screen(void);

void log_panic(const char *c);

#endif