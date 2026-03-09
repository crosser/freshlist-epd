#include <stdint.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_types.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_uc8179.h>
#include <lvgl.h>

#include "display.h"
#include "sdkconfig.h"

#define TAG "uc8179_demo"

#if defined(CONFIG_HWE_DISPLAY_SPI1_HOST)
# define SPIx_HOST SPI1_HOST
#elif defined(CONFIG_HWE_DISPLAY_SPI2_HOST)
# define SPIx_HOST SPI2_HOST
#else
# error "SPI host 1 or 2 must be selected"
#endif

#if defined(CONFIG_HWE_DISPLAY_RST_ACTIVE_LEVEL_LOW)
# define CONFIG_HWE_DISPLAY_RST_ACTIVE_LEVEL 0
#elif defined(CONFIG_HWE_DISPLAY_RST_ACTIVE_LEVEL_HIGH)
# define CONFIG_HWE_DISPLAY_RST_ACTIVE_LEVEL 1
#else
# error "RST_ACTIVE_LEVEL must be selected"
#endif

#if defined(CONFIG_HWE_DISPLAY_BUSY_LEVEL_LOW)
# define CONFIG_HWE_DISPLAY_BUSY_LEVEL 0
#elif defined(CONFIG_HWE_DISPLAY_BUSY_LEVEL_HIGH)
# define CONFIG_HWE_DISPLAY_BUSY_LEVEL 1
#else
# error "BUSY level must be selected"
#endif

#define BITMAP_SIZE (CONFIG_HWE_DISPLAY_WIDTH * CONFIG_HWE_DISPLAY_HEIGHT / 8)
#define LV_BUF_SIZE (BITMAP_SIZE + 8)
#define LV_TICK_PERIOD_MS 1

static bool color_trans_done(void *user_ctx)
{
	lv_display_t *disp = (lv_display_t*)user_ctx;
	// ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));
	lv_display_flush_ready(disp);
	return false;
}

static void lv_tick_task(void *arg)
{
	lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area,
		uint8_t *px_map)
{
	esp_lcd_panel_handle_t panel_handle =
		(esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
	ESP_LOGI(TAG, "Flush callback called (%d %d %d %d),"
			" drawing bitmap on handle %p",
			area->x1, area->y1, area->x2, area->y2, panel_handle);
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, px_map, 8, ESP_LOG_INFO);
	ESP_LOG_BUFFER_HEX_LEVEL(TAG, px_map + 8, 16, ESP_LOG_INFO);
	ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
			area->x1, area->y1, area->x2 + 1, area->y2 + 1,
			px_map + 8));
}

void app_main(void)
{
	ESP_LOGI(TAG, "Initializing SPI bus");
	ESP_ERROR_CHECK(spi_bus_initialize(SPIx_HOST,
		&(spi_bus_config_t) {
			.mosi_io_num = CONFIG_HWE_DISPLAY_SPI_MOSI,
			.miso_io_num = CONFIG_HWE_DISPLAY_SPI_MISO,
			.sclk_io_num = CONFIG_HWE_DISPLAY_SPI_SCK,
			.quadwp_io_num = -1,
			.quadhd_io_num = -1,
			.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE,
			.flags = SPICOMMON_BUSFLAG_MASTER
				| SPICOMMON_BUSFLAG_GPIO_PINS,
		},
		SPI_DMA_CH_AUTO
	));
	ESP_LOGI(TAG, "Attach panel IO handle to SPI");
	esp_lcd_panel_io_handle_t io_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
		(esp_lcd_spi_bus_handle_t)SPIx_HOST,
		&(esp_lcd_panel_io_spi_config_t) {
			.cs_gpio_num = CONFIG_HWE_DISPLAY_SPI_CS,
			.dc_gpio_num = CONFIG_HWE_DISPLAY_SPI_DC,
			.spi_mode = 0,
			.pclk_hz = CONFIG_HWE_DISPLAY_SPI_FREQUENCY,
			.lcd_cmd_bits = 8,
			.lcd_param_bits = 8,
			.trans_queue_depth = 17,
		},
		&io_handle
	));
	// NOTE: Please call gpio_install_isr_service() manually
	// before esp_lcd_new_panel_uc8179() because gpio_isr_handler_add()
	// is called in esp_lcd_new_panel_uc8179()
	ESP_LOGI(TAG, "Install ISR service for GPIO");
	gpio_install_isr_service(0);
	ESP_LOGI(TAG, "Initialize uc8179 panel driver");
	esp_lcd_panel_handle_t panel_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_uc8179(io_handle,
		&(esp_lcd_panel_dev_config_t) {
			.reset_gpio_num = CONFIG_HWE_DISPLAY_RST,
			.flags.reset_active_high =
				CONFIG_HWE_DISPLAY_RST_ACTIVE_LEVEL,
			.vendor_config =
				&(esp_lcd_uc8179_config_t) {
					.led_gpio_num =CONFIG_HWE_DISPLAY_LED,
					.busy_gpio_num =
						CONFIG_HWE_DISPLAY_BUSY,
					.busy_gpio_lvl =
						CONFIG_HWE_DISPLAY_BUSY_LEVEL,
					.width = CONFIG_HWE_DISPLAY_WIDTH,
					.height = CONFIG_HWE_DISPLAY_HEIGHT,
				},
		},
		&panel_handle));
	ESP_LOGI(TAG, "Resetting e-Paper display...");
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
	vTaskDelay(pdMS_TO_TICKS(100));
	ESP_LOGI(TAG, "Initializing e-Paper display...");
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
	vTaskDelay(pdMS_TO_TICKS(100));
	// ESP_ERROR_CHECK(epaper_panel_set_custom_lut(panel_handle, ...);
	// vTaskDelay(pdMS_TO_TICKS(100));

	// SemaphoreHandle_t epd_ready = xSemaphoreCreateBinary();
	// Semaphore is created initially "taken"
	ESP_LOGI(TAG, "Preparing lvgl display...");
	lv_init();
	lv_display_t *disp = lv_display_create(CONFIG_HWE_DISPLAY_WIDTH,
			CONFIG_HWE_DISPLAY_HEIGHT);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
	lv_display_set_user_data(disp, panel_handle);
	lv_display_set_flush_cb(disp, disp_flush_cb);
	static lv_color_t *buf[2];
	for (int i = 0; i < 2; i++) {
		buf[i] = heap_caps_malloc(LV_BUF_SIZE, MALLOC_CAP_DMA);
		assert(buf[i] != NULL);
		memset(buf[i], 0, LV_BUF_SIZE);
	}
	lv_display_set_buffers(disp, buf[0], buf[1], LV_BUF_SIZE,
			LV_DISPLAY_RENDER_MODE_FULL);
	ESP_ERROR_CHECK(epd_register_event_callbacks(
		panel_handle,
		&(epd_io_callbacks_t) {
			.on_color_trans_done = color_trans_done,
		},
		disp));
	esp_timer_handle_t lv_tick_timer;
	ESP_ERROR_CHECK(esp_timer_create(
		&(esp_timer_create_args_t) {
			.callback = lv_tick_task,
			.name = "lvgl tick task",
		},
		&lv_tick_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(lv_tick_timer,
				LV_TICK_PERIOD_MS * 1000));
	ESP_LOGI(TAG, "Initializing LVGL Display...");
	init_display(disp);
	ESP_LOGI(TAG, "Going into update loop...");
	while (true) {
		vTaskDelay(pdMS_TO_TICKS(10));
		lv_task_handler();
	}
	ESP_LOGI(TAG, "Loop finished");
	stop_display(disp);
	esp_deep_sleep_start();
}
