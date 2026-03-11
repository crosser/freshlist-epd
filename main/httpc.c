#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#ifdef DEBUG_HTTPC
#  define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif
#include <esp_log.h>
#include <esp_http_client.h>
// #include <esp_tls.h>
#include "httpc.h"
#include "lvscreen.h"

#if defined __has_include
#  if __has_include("../url.h")
#    include "../url.h"
#  endif
#endif

#include "sdkconfig.h"

#ifdef LOCAL_URL
#  define URL LOCAL_URL
#else
#  define URL CONFIG_URL
#endif

#define TAG "httpc"

#define BUFFER_SIZE 256

RTC_DATA_ATTR static struct tm when_last = {};

char *junk[] = {
	".1080p",
	".720p",
	".HEVC",
	".BDRIP",
	".WEBRIP",
	NULL,
};

typedef struct {
	QueueHandle_t stream;
	SemaphoreHandle_t done;
	esp_err_t ret;
} http_ctx_t;

typedef struct {
	int index;
	char *buffer;
	size_t capacity;
	size_t current;
	QueueHandle_t stream;  // We need a copy because http_ctx will go
	http_ctx_t *http_ctx;
} data_ctx_t;

static void show_entry(int n, char *pfx, char *msg)
{
	ESP_LOGD(TAG, "%d: pfx=%s, msg=%s", n, pfx, msg);
	struct tm when = {};
	char tbuf[32] = {};
	strptime(pfx, "%a %b %d %T %Y", &when);
	strftime(tbuf, sizeof(tbuf), "%d %H:%M", &when);

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

	ESP_LOGI(TAG, "Compressed: %d: pfx=%s, msg=%s", n, pfx, msg);
}

static void process_line(int n, char *l)
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
	show_entry(n, e[0], e[1]);
}

static inline bool eol(char c)
{
	return (c == '\n' || c == '\r');
}

static void process_data(data_ctx_t *data_ctx, size_t len, char *chunk)
{
	char *b = chunk, *e = chunk + len;
	char *ln;
	size_t sz;
	bool got_nl;

	do {
		for (e = b; e < chunk + len && !eol(*e); e++) /* nothing */ ;
		// now e points to the character beyond the end of the line
		got_nl = (e < chunk + len);  // Not ran into the end yet
		sz = e - b;
		while (e < chunk + len && eol(*e)) *(e++) = '\0';
		ESP_LOGD(TAG,
			"Slice b=%p, e=%p, end=%p, got_nl=%d, size=%d: %s",
			b, e, chunk + len, got_nl, sz, b?b:"NULL");
		// Now b points to a null-terminated line. Do we gave leftover?
		if (data_ctx->current) {
			ESP_LOGD(TAG, "We have letover \"%.*s\" and"
					" new data \"%.*s\"",
					data_ctx->current, data_ctx->buffer,
					sz, b);
			ln = data_ctx->buffer;
			if (len) {  // Got anything at all or is is fin?
				if (data_ctx->current + sz
						> data_ctx->capacity) {
					ESP_LOGE(TAG, "Too much data %d", sz);
					memcpy(ln, b, data_ctx->capacity
							- data_ctx->current);
					data_ctx->current = data_ctx->capacity;
				} else {
					memcpy(data_ctx->buffer
						+ data_ctx->current,
						b, sz);
					data_ctx->current += sz;
				}
			}
			// We have reserved an extra byte there
			ln[data_ctx->current] = '\0';
			ESP_LOGD(TAG, "Combined \"%s\"", ln);
		} else {
			ln = b;
			ESP_LOGD(TAG, "Complete \"%s\"", ln);
		}
		// Now we have a nul-terminated line `ln`. If it was newline
		// terminated, _or_ came with len == 0 (EVENT_FINISHED),
		// pass it to the function. Otherwise save in the data_ctx.
		if (got_nl || !len) {
			if (ln) {  // FIN could happen with empty save-data
				// process_line(data_ctx->index++, ln);
				char *mln = strdup(ln);
				xQueueSend(data_ctx->stream, &mln,
						portMAX_DELAY);
			}
			data_ctx->current = 0;
		} else {  // Don't call process_line, but save it
			ESP_LOGD(TAG, "Saving incomplete line \"%.*s\"",
					sz, b);
			if (sz > data_ctx->capacity) {
				ESP_LOGE(TAG, "line longer than buffer %d",
						sz);
				memcpy(data_ctx->buffer, b,
						data_ctx->capacity);
				data_ctx->current = data_ctx->capacity;
			} else {
				memcpy(data_ctx->buffer, b, sz);
				data_ctx->current = sz;
			}
		}
		b = e;
	} while (b < chunk + len);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
	data_ctx_t *data_ctx = (data_ctx_t *)evt->user_data;

	switch(evt->event_id) {
	case HTTP_EVENT_ON_CONNECTED:
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_CONNECTED");
		break;
	case HTTP_EVENT_HEADERS_SENT:
		ESP_LOGI(TAG, "Event HTTP_EVENT_HEADERS_SENT");
		break;
	case HTTP_EVENT_ON_HEADER:
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_HEADER");
		break;
	case HTTP_EVENT_ON_HEADERS_COMPLETE:
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_HEADERS_COMPLETE");
		when_last = (struct tm){};
		break;
	case HTTP_EVENT_ON_STATUS_CODE:
		// Apparently len == 4 and data is 32bit status code?
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_STATUS_CODE %d",
				*(uint32_t*)(evt->data));
		if (*(uint32_t*)(evt->data) == 200) {
			data_ctx->http_ctx->ret = ESP_ERR_NOT_FINISHED;
			xSemaphoreGive(data_ctx->http_ctx->done);
			data_ctx->http_ctx = NULL; // it is in parent stack
		}
		break;
	case HTTP_EVENT_ON_DATA:
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_DATA len=%d",
				evt->data_len);
		ESP_LOGD(TAG, "Event HTTP_EVENT_ON_DATA data=%.*s",
				evt->data_len, evt->data);
		process_data(data_ctx, evt->data_len, evt->data);
		break;
	case HTTP_EVENT_ON_FINISH:
		ESP_LOGI(TAG, "Event HTTP_EVENT_ON_FINISH");
		process_data(data_ctx, 0, NULL);
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "Event HTTP_EVENT_DISCONNECTED");
		break;
	case HTTP_EVENT_REDIRECT:
		ESP_LOGI(TAG, "Event HTTP_EVENT_REDIRECT");
		break;
	default:
		ESP_LOGI(TAG, "Event %d", evt->event_id);
		break;
	}
	return ESP_OK;
}

static void httpc_run(void *user_ctx)
{
	http_ctx_t *http_ctx_ptr = user_ctx;
	char buf[BUFFER_SIZE] = {0};
	data_ctx_t data_ctx = {
		.index = 0,
		.buffer = buf,
		.capacity = BUFFER_SIZE - 1,
		.current = 0,
		.stream = http_ctx_ptr->stream,
		.http_ctx = http_ctx_ptr,
	};

#ifdef DEBUG_HTTPC
	esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
	ESP_LOGI(TAG, "connecting to %s", URL);
	esp_http_client_handle_t client = esp_http_client_init(
		&(esp_http_client_config_t){
			.url = URL,
			.event_handler = http_event_handler,
			.user_data = &data_ctx,
		}
	);
	// esp_http_client_set_header(client, "If-Modified-Since", c_last);

	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRId64,
				esp_http_client_get_status_code(client),
				esp_http_client_get_content_length(client));
	} else {
		ESP_LOGE(TAG, "HTTP GET request failed: %s",
				esp_err_to_name(err));
		http_ctx_ptr->ret = err;
	}
	void *null = NULL;
	xQueueSend(data_ctx.stream, &null, portMAX_DELAY);

	esp_http_client_cleanup(client);
	ESP_LOGI(TAG, "Httpc task did the deed and is about to finish");
	if (data_ctx.http_ctx) {  // maybe already given from the cb
		xSemaphoreGive(http_ctx_ptr->done);
	}
	vTaskDelete(NULL);
}

esp_err_t httpc(QueueHandle_t stream)
{
	http_ctx_t http_ctx = {
		.stream = stream,
		.done = xSemaphoreCreateBinary(),
		.ret = ESP_OK,
	};
	xTaskCreate(httpc_run, "httpc_task", 4096*2, &http_ctx, 0, NULL);
	ESP_LOGI(TAG, "Httpc task launched");
	xSemaphoreTake(http_ctx.done, portMAX_DELAY);
	vSemaphoreDelete(http_ctx.done);
	ESP_LOGI(TAG, "Httpc returning, with task possibly running");
	return http_ctx.ret;
}
