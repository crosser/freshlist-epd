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
		LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0, 0, 0)),
		LV_STYLE_CONST_BG_OPA(LV_OPA_100),
		LV_STYLE_CONST_TEXT_FONT(&UbuntuSansMono),
		LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(255, 255, 255)),
		LV_STYLE_CONST_PROPS_END,
	}));

static struct panes {
	lv_obj_t *title;
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
	strptime(pfx, "%a %b %d %T %Y", &when);
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

void stop_screen(lv_display_t *disp)
{
	ESP_LOGI(TAG, "stop_display");
	lv_obj_t *scr = lv_display_get_screen_active(disp);
	lv_obj_clean(scr);
}
