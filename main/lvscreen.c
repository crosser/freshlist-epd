#include <stdlib.h>
#include <time.h>
#include <esp_log.h>
#include <lvgl.h>
#include <misc/lv_style.h>
#include "lvscreen.h"
#include "sdkconfig.h"

static const char *TAG = "LV display";

#define LINE_HEIGHT (CONFIG_HWE_DISPLAY_HEIGHT / (DISPLAY_ROWS + 2))
#define PFX_WIDTH (CONFIG_HWE_DISPLAY_WIDTH * 2 / 7)
#define MSG_WIDTH (CONFIG_HWE_DISPLAY_WIDTH - (CONFIG_HWE_DISPLAY_WIDTH / 4))
#define IND_WIDTH (CONFIG_HWE_DISPLAY_WIDTH / 10)

LV_FONT_DECLARE(UbuntuSans);
LV_FONT_DECLARE(UbuntuSansMono);

static LV_STYLE_CONST_INIT(screen_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_PROPS_END,
	}));

static LV_STYLE_CONST_INIT(main_pfx_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_HEIGHT(LINE_HEIGHT),
		LV_STYLE_CONST_WIDTH(PFX_WIDTH),
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_FONT(&UbuntuSansMono),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_PROPS_END,
	}));

static LV_STYLE_CONST_INIT(main_msg_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_HEIGHT(LINE_HEIGHT),
		LV_STYLE_CONST_WIDTH(MSG_WIDTH),
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_FONT(&UbuntuSans),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_PROPS_END,
	}));

static LV_STYLE_CONST_INIT(status_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_HEIGHT(LINE_HEIGHT),
		LV_STYLE_CONST_WIDTH(LV_PCT(84)),
		LV_STYLE_CONST_TEXT_ALIGN(LV_TEXT_ALIGN_CENTER),
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_FONT(&UbuntuSansMono),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_PROPS_END,
	}));

static LV_STYLE_CONST_INIT(indicator_style,
	((static lv_style_const_prop_t []){
		LV_STYLE_CONST_HEIGHT(LINE_HEIGHT),
		LV_STYLE_CONST_WIDTH(LV_PCT(8)),
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_FONT(&UbuntuSansMono),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_PROPS_END,
	}));

static void battery_draw_cb(lv_event_t * e)
{
	lv_obj_t *obj = lv_event_get_target(e);
	int value = (intptr_t)lv_obj_get_user_data(obj);
	lv_layer_t *layer = lv_event_get_layer(e);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);
	lv_area_t a = { .x1 = 2, .x2 = 60, .y1 = 2, .y2 = 26, };
	lv_area_align(&obj_coords, &a, LV_ALIGN_TOP_RIGHT, 0, 0);

	lv_draw_rect_dsc_t box;
	lv_draw_rect_dsc_init(&box);
	box.border_width = 3;
	box.border_color = lv_color_make(255, 255, 255);
	box.bg_opa = LV_OPA_0;
	lv_draw_rect(layer, &box, &a);
	a.x1 += 2;
	a.x2 = a.x1 + (value * 56 / 100) - 4;
	a.y1 += 2;
	a.y2 -= 2;
	lv_draw_rect_dsc_t inside;
	lv_draw_rect_dsc_init(&inside);
	inside.border_width = 0;
	inside.bg_color = lv_color_make(255, 255, 255);
	lv_draw_rect(layer, &inside, &a);
}

static void signal_draw_cb(lv_event_t * e)
{
	lv_obj_t *obj = lv_event_get_target(e);
	int value = (intptr_t)lv_obj_get_user_data(obj);
	lv_layer_t *layer = lv_event_get_layer(e);
	lv_area_t obj_coords;
	lv_obj_get_coords(obj, &obj_coords);

	lv_draw_rect_dsc_t bar;
	lv_draw_rect_dsc_init(&bar);
	bar.border_width = 1;
	lv_area_t a;
	lv_color_t black = lv_color_make(255, 255, 255);
	lv_color_t white = lv_color_make(0, 0, 0);
	bar.border_color = black;
	for (int i = 0; i < 5; i++) {
		bar.bg_color = (i <= value) ? black : white;
		a.x1 = obj_coords.x1 + i * 9;
		a.x2 = a.x1 + 5;
		a.y1 = obj_coords.y1 + (4 - i) * 5;
		a.y2 = 22;
		// lv_area_align(&obj_coords, &a, LV_ALIGN_TOP_LEFT,
		//		i * 9 + 8, 0);
		lv_draw_rect(layer, &(bar), &a);
	}
}

static struct panes {
	lv_obj_t *signal;
	lv_obj_t *title;
	lv_obj_t *battery;
	struct {
		lv_obj_t *pfx;
		lv_obj_t *msg;
	} main[DISPLAY_ROWS];
	lv_obj_t *status;
} panes = {0};

char *junk[] = {
	".1080p",
	".720p",
	".HEVC",
	".BDRIP",
	".WEBRIP",
	NULL,
};

static void show_status(lv_display_t *disp, char *msg)
{
	ESP_LOGD(TAG, "status msg=%s", msg);
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf),
			"%c %z", &timeinfo);
	lv_obj_t *lbl;
	lbl = panes.title;
	lv_obj_clean(lbl);
	lv_label_set_text(lbl, strftime_buf);
	lbl = panes.status;
	lv_obj_clean(lbl);
	lv_label_set_text_fmt(lbl, "Last: %s", msg);
}

static void show_entry(lv_display_t *disp, int n, char *pfx, char *msg)
{
	ESP_LOGD(TAG, "%d: pfx=%s, msg=%s", n, pfx, msg);
	struct tm when = {};
	char tbuf[32] = {};
	strptime(pfx, "%Y-%m-%d %T%z", &when);
	strftime(tbuf, sizeof(tbuf), "%m-%d %H:%M", &when);

	// Let's compress the message
	char *r, *w, *dot = NULL;
	enum {
		pass,
		wparn,
		wbrkt,
	} state;
	for (r=msg, w=msg, state = pass; *r; r++) {
		if (state == pass) switch (*r) {
			case '[': state = wbrkt; break;
			case '(': state = wparn; break;
			default: break;
		}
		if (state == pass) {
			if (*r == '.') dot = w;
			*(w++) = *r;
		}
		switch (state) {
		case wparn:
			if (*r == ')') state = pass;
			break;
		case wbrkt:
			if (*r == ']') state = pass;
			break;
		default:
			break;
		}
	}
	if (dot) w = dot;
	*(w--) = '\0';
	while (w > msg && *w == ' ') *(w--) = '\0';
	// Let's try to get rid of more non-essential text
	for (char **s = junk; *s; s++) {
		if ((w = strstr(msg, *s))) {
			*w = '\0';
		}
	}
	// Finally, there may be whitespace at the beginning
	while (*msg == ' ') msg++;  // Guaranteed to terminate on NUL

	ESP_LOGI(TAG, "Compressed: %d: tbuf=%s, msg=%s", n, tbuf, msg);
	lv_obj_t *lbl;
	lbl = panes.main[n].pfx;
	lv_obj_clean(lbl);
	lv_label_set_text(lbl, tbuf);
	lbl = panes.main[n].msg;
	lv_obj_clean(lbl);
	lv_label_set_text(lbl, msg);
}

static void process_line(lv_display_t *disp, int n, char *l)
{
	char *r, *w, *f = l;
	bool in, quote = false;
	int i = 0;
	char *e[2] = {};

	ESP_LOGD(TAG, "Line %d: %s", n, l);
	for (r=l, w=l, in=false; *r; r++) {
		switch (*r) {
		case '"':
			if (quote) *(w++) = *r;
			quote = in;
			in = !in;
			break;
		case ',':
			quote = false;
			if (!in) {
				*(w++) = '\0';
				if (i < 2) e[i++] = f;
				else ESP_LOGE(TAG, "csv %d: %s", i, f);
				f = w;
			}
			break;
		default:
			quote = false;
			*(w++) = *r;
			break;
		}
	}
	*w = '\0';
	if (i < 2) e[i++] = f;
	else ESP_LOGE(TAG, "csv %d: %s", i, f);
	if (n) show_entry(disp, n - 1, e[0], e[1]);
	else show_status(disp, e[0]);
}

void init_screen(lv_display_t *disp)
{
	ESP_LOGI(TAG, "init_display");

	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c %z", &timeinfo);
	ESP_LOGI(TAG, "Time is %s", strftime_buf);

	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_add_style(scr, &screen_style, LV_PART_MAIN);
	lv_obj_clean(scr);

	lv_obj_t *obj;
	obj = lv_label_create(scr);
	lv_obj_add_style(obj, &status_style, LV_PART_MAIN);
	lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 0);
	lv_label_set_text_static(obj, " ");
	panes.title = obj;
	obj = lv_label_create(scr);
	lv_obj_add_style(obj, &indicator_style, LV_PART_MAIN);
	lv_obj_align_to(obj, panes.title, LV_ALIGN_OUT_LEFT_MID, 0, 0);
	lv_label_set_text_static(obj, " ");
	lv_obj_add_event_cb(obj, signal_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
	panes.signal = obj;
	obj = lv_label_create(scr);
	lv_obj_add_style(obj, &indicator_style, LV_PART_MAIN);
	lv_obj_align_to(obj, panes.title, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
	lv_label_set_text_static(obj, " ");
	lv_obj_add_event_cb(obj, battery_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
	panes.battery = obj;
	for (int i = 0; i < DISPLAY_ROWS; i++) {
		obj = lv_label_create(scr);
		lv_obj_add_style(obj, &main_pfx_style, LV_PART_MAIN);
		if (i) {
			lv_obj_align_to(obj, panes.main[i-1].pfx,
					LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
		} else {
			lv_obj_align(obj, LV_ALIGN_OUT_TOP_MID,
					0, LINE_HEIGHT * 2);
		}
		lv_label_set_text_static(obj, " ");
		panes.main[i].pfx = obj;

		obj = lv_label_create(scr);
		lv_obj_add_style(obj, &main_msg_style, LV_PART_MAIN);
		lv_obj_align_to(obj, panes.main[i].pfx,
				LV_ALIGN_OUT_RIGHT_MID, 2, 0);
		lv_label_set_text_static(obj, " ");
		panes.main[i].msg = obj;
	}
	obj = lv_label_create(scr);
	lv_obj_add_style(obj, &status_style, LV_PART_MAIN);
	lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_label_set_text_static(obj, " ");
	panes.status = obj;
}

void write_screen(lv_display_t *disp, int row, char *msg)
{
	ESP_LOGI(TAG, "Drawing line %d: %s", row, msg);
	if (row >= DISPLAY_ROWS) {
		ESP_LOGI(TAG, "draw row number %d is too big: ceiling %d",
				row, DISPLAY_ROWS);
		return;
	}
	process_line(disp, row, msg);
}

void write_battery_rssi(lv_display_t *disp, int mV, int rssi)
{
	lv_obj_t *lbl;
	lbl = panes.battery;
	lv_obj_clean(lbl);
#if 0
	lv_label_set_text_fmt(lbl, "%d", mV);
#else
	int level = (mV - CONFIG_BATTERY_ADC_MIN) * 100 /
		(CONFIG_BATTERY_ADC_MAX - CONFIG_BATTERY_ADC_MIN);
	if (level < 0) level = 0;
	if (level > 100) level = 100;
	ESP_LOGI(TAG, "Drawing battery %d%%", level);
	lv_obj_set_user_data(lbl, (void*)level);
	lv_obj_invalidate(lbl);
#endif
	lbl = panes.signal;
	lv_obj_clean(lbl);
#if 0
	lv_label_set_text_fmt(lbl, "%d", rssi);
#else
	int signal = (100 + rssi) / 10;
	if (signal < 0) signal = 0;
	if (signal > 4) signal = 4;
	ESP_LOGI(TAG, "Drawing signal %d bars", signal);
	lv_obj_set_user_data(lbl, (void*)signal);
	lv_obj_invalidate(lbl);
#endif
}

void stop_screen(lv_display_t *disp)
{
	ESP_LOGI(TAG, "stop_display");
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_clean(scr);
}
