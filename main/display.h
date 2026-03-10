#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <lvgl.h>

#define DISPLAY_ROWS 6

void init_display(lv_display_t *disp);
void stop_display(lv_display_t *disp);

#endif
