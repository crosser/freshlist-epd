#ifndef _LVSCREEN_H
#define _LVSCREEN_H

#include <lvgl.h>

#define DISPLAY_ROWS 6

void init_screen(lv_display_t *disp);
void stop_screen(lv_display_t *disp);

#endif
