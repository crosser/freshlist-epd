#ifndef _LVSCREEN_H
#define _LVSCREEN_H

#include <lvgl.h>

#define DISPLAY_ROWS 8

void init_screen(lv_display_t *disp);
void write_screen(lv_display_t *disp, int linecount, char *line);
void write_battery(lv_display_t *disp, int value);
void stop_screen(lv_display_t *disp);

#endif
