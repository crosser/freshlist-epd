#include <stdint.h>
#include <stdio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "wifi.h"
#include "panel.h"

#define TAG "freshlist"

#define SLEEP_TIME (60 * 23)

void app_main(void)
{
	ESP_LOGI(TAG, "Initialize wifi");
	init_wifi();
	ESP_LOGI(TAG, "Initialize display panel");
	init_panel();
	ESP_LOGI(TAG, "Deinitialize wifi");
	deinit_wifi();
	ESP_LOGI(TAG, "Going to deep sleep for %d sec", SLEEP_TIME);
	esp_deep_sleep(1000000LL * SLEEP_TIME);
}
