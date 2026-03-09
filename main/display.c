#include <esp_log.h>
#include <lvgl.h>
#include <misc/lv_style.h>
#include "display.h"

static const char *TAG = "LV display";

static LV_STYLE_CONST_INIT(screen_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_PROPS_END,
	}));

static LV_STYLE_CONST_INIT(lbl_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_PAD_TOP(10),
		LV_STYLE_CONST_PAD_BOTTOM(10),
		LV_STYLE_CONST_PAD_LEFT(10),
		LV_STYLE_CONST_PAD_RIGHT(10),
		LV_STYLE_CONST_RADIUS(20),
		LV_STYLE_CONST_BORDER_WIDTH(5),
		LV_STYLE_CONST_BORDER_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_BORDER_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_ALIGN(LV_ALIGN_CENTER),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_0),
		LV_STYLE_CONST_PROPS_END,
	}));

static void draw_cb(lv_event_t *e)
{
	lv_obj_t *obj = lv_event_get_target(e);
	lv_draw_task_t *draw_task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
	ESP_LOGI(TAG, "Draw task called, part %d, expect not %d",
			base_dsc->part, LV_PART_MAIN);
	if (base_dsc->part != LV_PART_MAIN) return;

	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);
	lv_draw_line_dsc_t line;
	lv_draw_line_dsc_init(&line);
	line.color = lv_color_make(255, 255, 255);
	line.width = 5;
	line.p1.x = obj_coords.x1 + 5;
	line.p1.y = obj_coords.y1 + 5;
	line.p2.x = obj_coords.x2 - 5;
	line.p2.y = obj_coords.y2 - 5;
	lv_draw_line(base_dsc->layer, &line);
	line.p1.x = obj_coords.x1 + 5;
	line.p1.y = obj_coords.y2 - 5;
	line.p2.x = obj_coords.x2 - 5;
	line.p2.y = obj_coords.y1 + 5;
	lv_draw_line(base_dsc->layer, &line);
}

void init_display(lv_display_t *disp)
{
	ESP_LOGI(TAG, "init_display");
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_add_style(scr, &screen_style, LV_PART_MAIN);
	lv_obj_clean(scr);

	lv_obj_t *frm = lv_label_create(scr);
	lv_obj_add_style(frm, &lbl_style, LV_PART_MAIN);
	lv_obj_set_size(frm, lv_pct(90), lv_pct(90));
	lv_obj_align(frm, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_align(frm, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
	lv_label_set_text_static(frm, " ");
	lv_obj_add_event_cb(frm, draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
	lv_obj_add_flag(frm, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	// lv_obj_invalidate(frm);

	lv_obj_t *lbl = lv_label_create(scr);
	lv_obj_add_style(lbl, &lbl_style, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(lbl, LV_OPA_100, LV_PART_MAIN);
	lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
	lv_label_set_text_static(lbl, "LVGL");
}

void stop_display(lv_display_t *disp)
{
	ESP_LOGI(TAG, "stop_display");
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_clean(scr);
}
