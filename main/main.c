#include <stdint.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "wifi.h"
#include "httpc.h"
#include "panel.h"

#define TAG "freshlist"

#define SLEEP_TIME (60 * 23)

void app_main(void)
{
	esp_err_t res;
	void *null = NULL;

	QueueHandle_t stream = xQueueCreate(4, sizeof(void*));
	ESP_LOGI(TAG, "Initialize wifi");
	if ((res = init_wifi()) == ESP_OK) {
		res = httpc(stream);
	}
	if (res != ESP_OK) {
		if (res != ESP_ERR_NOT_FINISHED) {
			/* It is a real error, push a message to the queue */
			char *merr = strdup(esp_err_to_name(res));
			xQueueSend(stream, &merr, portMAX_DELAY);
			xQueueSend(stream, &null, portMAX_DELAY);
		}
		char *line;
		for (xQueueReceive(stream, &line, portMAX_DELAY);
				line;
				xQueueReceive(stream, &line, portMAX_DELAY))
		{
			ESP_LOGI(TAG, "Line from queue: %s", line);
			free(line);
		}
		ESP_LOGI(TAG, "Initialize display panel");
		run_panel(stream);
	}
	ESP_LOGI(TAG, "Deinitialize wifi");
	deinit_wifi();
	vQueueDelete(stream);
	ESP_LOGI(TAG, "Going to deep sleep for %d sec", SLEEP_TIME);
	esp_deep_sleep(1000000LL * SLEEP_TIME);
}
