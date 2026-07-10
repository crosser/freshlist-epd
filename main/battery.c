#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include <driver/gpio.h>

#include "sdkconfig.h"
#include "battery.h"

#define TAG "BATTERY"

/* https://wiki.seeedstudio.com/ogdiy_kit_works_with_arduino/ */

static adc_channel_t channel;
static adc_oneshot_unit_handle_t handle;
static adc_cali_handle_t cali;

void init_battery_adc(void)
{
#if (CONFIG_HWE_BATTERY_ADC > -1)
#  if (CONFIG_HWE_BATTERY_ADC_ENABLE > -1)
	ESP_ERROR_CHECK(gpio_set_direction(CONFIG_HWE_BATTERY_ADC_ENABLE,
						GPIO_MODE_OUTPUT));
	ESP_ERROR_CHECK(gpio_set_level(CONFIG_HWE_BATTERY_ADC_ENABLE, 1));
#  endif
	adc_unit_t unit;
	ESP_ERROR_CHECK(adc_oneshot_io_to_channel(CONFIG_HWE_BATTERY_ADC,
						&unit, &channel));
	ESP_LOGD(TAG, "GPIO %d gave us unit %d chan %d",
				CONFIG_HWE_BATTERY_ADC, unit, channel);
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(
			&(adc_cali_curve_fitting_config_t){
				.unit_id = unit,
				.bitwidth = ADC_BITWIDTH_DEFAULT,
				.atten = ADC_ATTEN_DB_12,
			},
			&cali));
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t){
				.unit_id = unit,
				.ulp_mode = ADC_ULP_MODE_DISABLE,
			},
			&handle));
	ESP_ERROR_CHECK(adc_oneshot_config_channel(handle,
			channel,
			&(adc_oneshot_chan_cfg_t){
				.bitwidth = ADC_BITWIDTH_DEFAULT,
				.atten = ADC_ATTEN_DB_12,
			}));
#endif
}

int battery(void)
{
	int value = 0;
#if (CONFIG_HWE_BATTERY_ADC > -1)
	ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(
			handle, cali, channel, &value));
	ESP_LOGI(TAG, "Calibrated result: %d", value);
#endif
	return value;
}

void stop_battery_adc(void)
{
#if (CONFIG_HWE_BATTERY_ADC > -1)
#  if (CONFIG_HWE_BATTERY_ADC_ENABLE > -1)
	ESP_ERROR_CHECK(gpio_set_level(CONFIG_HWE_BATTERY_ADC_ENABLE, 0));
#  endif
#endif
}
