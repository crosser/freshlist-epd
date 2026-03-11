/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdlib.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_netif_sntp.h>
#include <esp_netif_net_stack.h>
#include <lwip/sys.h>
#include <lwip/err.h>
#include <lwip/dhcp6.h>
#include <nvs_flash.h>
#include "wifi.h"
#include "lvscreen.h"

struct wifi_creds {
        char *ssid;
	char *password;
};

#if defined __has_include
#  if __has_include("../credentials.h")
#    include "../credentials.h"
#    define WIFI_CREDS_DEFINED
#  endif
#endif

#include "sdkconfig.h"

#ifndef WIFI_CREDS_DEFINED
static struct wifi_creds wifi_creds[] = {
	{ .ssid = CONFIG_WIFI_SSID, .password = CONFIG_WPA_PASSWORD },
	{ NULL, NULL }
};
#endif

static const char *TAG = "WiFi";

#define HAVE_IPV4 BIT0
#define HAVE_IPV6 BIT1
#define WIFI_FAIL BIT2

static bool need_conn_action;
static int retries;
static esp_netif_t *wifi_netif;

static void on_ready(void *arg)
{
	if (need_conn_action) {
		need_conn_action = false;
		ESP_ERROR_CHECK(esp_netif_sntp_init(
			&(esp_sntp_config_t)ESP_NETIF_SNTP_DEFAULT_CONFIG(
				"pool.ntp.org"
			)));
	} else {
		ESP_LOGI(TAG, "on_ready action taken already");
	}
}

static void sntp_event_handler(void* arg, esp_event_base_t event_base,
		int32_t event_id, void* event_data)
{
	SemaphoreHandle_t *ready_ptr = arg;
	assert(event_base == NETIF_SNTP_EVENT);
	ESP_LOGI(TAG, "NETIF_SNTP_EVENT");
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf),
			"%c %z", &timeinfo);
	ESP_LOGI(TAG, "Got time %s", strftime_buf);
	xSemaphoreGive(*ready_ptr);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
		int32_t event_id, void* event_data)
{
	uint16_t ap_num;
	assert(event_base == WIFI_EVENT);

	switch (event_id) {
	case WIFI_EVENT_STA_START:
		//draw_status(arg, "Scanning...");
		ESP_ERROR_CHECK(esp_wifi_scan_start(&(wifi_scan_config_t){
					.scan_type = WIFI_SCAN_TYPE_ACTIVE,
				}, false));
		break;
	case WIFI_EVENT_STA_STOP:
		time_t now;
		struct tm timeinfo;
		char strftime_buf[64];
		time(&now);
		localtime_r(&now, &timeinfo);
		strftime(strftime_buf, sizeof(strftime_buf),
				"%c %z", &timeinfo);
		ESP_LOGI(TAG, "Stopped at %s", strftime_buf);
		break;
	case WIFI_EVENT_SCAN_DONE:
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
		ESP_LOGI(TAG, "Scan returned %hu records", ap_num);
		wifi_ap_record_t *ap_records =
			malloc(sizeof(wifi_ap_record_t) * ap_num);
		if (!ap_records) {
			ESP_LOGE(TAG, "Failed to allocate scan results");
			// Mere presense of this even if not executed
			// corrupts video memory
			// ESP_ERROR_CHECK(esp_wifi_clear_ap_list());
			ESP_ERROR_CHECK(esp_wifi_stop());
			break;
		}
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(
					&ap_num, ap_records));
		int cred_id = -1;
		for (int i = 0; i < ap_num; i++) {
			ESP_LOGI(TAG, "AP %i: ssid %.33s, ch %i, rssi %d",
					i, ap_records[i].ssid,
					ap_records[i].primary,
					ap_records[i].rssi);
			char s_rssi[10];
			snprintf(s_rssi, sizeof(s_rssi), "%02u %02d",
					ap_records[i].primary,
					ap_records[i].rssi);
			char s_ssid[34];
			snprintf(s_ssid, sizeof(s_ssid), "%.33s",
					ap_records[i].ssid);
			if (cred_id < 0) {
				for (int j = 0; wifi_creds[j].ssid; j++) {
					if (!strncmp(
						(char *)ap_records[i].ssid,
						wifi_creds[j].ssid,
						33)) {
						cred_id = j;
						break;
					}
				}
			}
		}
		free(ap_records);
		if (cred_id < 0) {
			ESP_LOGI(TAG, "Did not find matching credentials");
			ESP_ERROR_CHECK(esp_wifi_stop());
			break;
		}
		ESP_LOGI(TAG, "Found known AP \"%s\", connecting",
				wifi_creds[cred_id].ssid);
		wifi_config_t config = {};
		strncpy((char *)config.sta.ssid,
				wifi_creds[cred_id].ssid, 32);
		strncpy((char *)config.sta.password,
				wifi_creds[cred_id].password, 64);
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
		need_conn_action = true;
		ESP_ERROR_CHECK(esp_wifi_connect());
		break;
	case WIFI_EVENT_STA_CONNECTED:
		wifi_event_sta_connected_t *conn =
			(wifi_event_sta_connected_t *)event_data;
		ESP_LOGI(TAG, "Connected %.*s", conn->ssid_len, conn->ssid);
		//char ssid_buf[33];
		//snprintf(ssid_buf, sizeof(ssid_buf),
		//		"%.*s", conn->ssid_len, conn->ssid);
		//draw_status(arg, ssid_buf);
		// Have to do this by hand for SLAAC to work!?
		ESP_ERROR_CHECK(esp_netif_create_ip6_linklocal(wifi_netif));
		ESP_ERROR_CHECK(dhcp6_enable_stateless(
				esp_netif_get_netif_impl(wifi_netif)));
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "Disconnected");
		if (retries-- > 0) {
			ESP_LOGI(TAG, "Disconnected, retries left: %d",
					retries);
			esp_wifi_connect();
		} else {
			ESP_LOGI(TAG, "Disconnected, stopping");
			ESP_ERROR_CHECK(esp_wifi_stop());
		}
		break;
	case WIFI_EVENT_HOME_CHANNEL_CHANGE:
		ESP_LOGI(TAG, "Channel change");
		break;
	default:
		ESP_LOGI(TAG, "Unexpected wifi event id %d", event_id);
		break;
	}
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
		int32_t event_id, void* event_data)
{
	char ip_buf[40];

	assert(event_base == IP_EVENT);
	switch (event_id) {
	case IP_EVENT_STA_GOT_IP:
		ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ipv4:" IPSTR,
				IP2STR(&event->ip_info.ip));
		//snprintf(ip_buf, sizeof(ip_buf),
		//		IPSTR, IP2STR(&event->ip_info.ip));
		on_ready(arg);
		break;
	case IP_EVENT_GOT_IP6:
		ip_event_got_ip6_t *event6 = (ip_event_got_ip6_t*) event_data;
		ESP_LOGI(TAG, "got ipv6:" IPV6STR,
				IPV62STR(event6->ip6_info.ip));
		if (esp_netif_ip6_get_addr_type(&event6->ip6_info.ip)
				== ESP_IP6_ADDR_IS_GLOBAL) {
			//ESP_LOGI(TAG, "It is global, can use!");
			//snprintf(ip_buf, sizeof(ip_buf), IPV6STR,
			//		IPV62STR(event6->ip6_info.ip));
			//draw_status(arg, ip_buf);
			on_ready(arg);
		}
		break;
	default:
		ESP_LOGI(TAG, "unexpected ip event id %d", event_id);
		break;
	}
}

esp_err_t init_wifi()
{
	setenv("TZ", CONFIG_TZSPEC, true);
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES
		|| ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_LOGI(TAG, "Starting WiFi");
	retries = 2;
	SemaphoreHandle_t ready = xSemaphoreCreateBinary();

	ESP_ERROR_CHECK(esp_netif_init());
	wifi_netif = esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(esp_wifi_init(
			&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
			ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
			IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
			IP_EVENT_GOT_IP6, &ip_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(NETIF_SNTP_EVENT,
			NETIF_SNTP_TIME_SYNC, &sntp_event_handler, &ready));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_LOGI(TAG, "handlers installed and mode set to STA");
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "cycle started, waiting for connectivity...");
	xSemaphoreTake(ready, portMAX_DELAY);
	ESP_LOGI(TAG, "Wifi fully initialized");
	vSemaphoreDelete(ready);
	return ESP_OK;
}

esp_err_t deinit_wifi(void)
{
	ESP_LOGI(TAG, "Disconnecting wifi");
	retries = 0;
	esp_netif_sntp_deinit();
	ESP_ERROR_CHECK(esp_wifi_disconnect());
	ESP_ERROR_CHECK(esp_event_handler_unregister(NETIF_SNTP_EVENT,
				NETIF_SNTP_TIME_SYNC, &sntp_event_handler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT,
				IP_EVENT_GOT_IP6, &ip_event_handler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT,
				IP_EVENT_STA_GOT_IP, &ip_event_handler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT,
				ESP_EVENT_ANY_ID, &wifi_event_handler));
	ESP_ERROR_CHECK(esp_event_loop_delete_default());
	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());
	ESP_LOGI(TAG, "wifi deinitialized");
	return ESP_OK;
}
