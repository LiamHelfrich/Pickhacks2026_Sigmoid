#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "microphone_uploader.h"
#include "network_rest.h"

#define TAG "simple_connect_example"

#define BLINK_GPIO GPIO_NUM_8

#define I2S_SEL_GPIO GPIO_NUM_7
#define I2S_LRCL_GPIO GPIO_NUM_6
#define I2S_DOUT_GPIO GPIO_NUM_4
#define I2S_BCLK_GPIO GPIO_NUM_5

static MicUploaderConfig mic_uploader_config = {
	.endpoint = "http://66.42.127.17:5000/upload",
	.i2s_sel_gpio = I2S_SEL_GPIO,
	.i2s_lrcl_gpio = I2S_LRCL_GPIO,
	.i2s_dout_gpio = I2S_DOUT_GPIO,
	.i2s_bclk_gpio = I2S_BCLK_GPIO,
	.blink_gpio = BLINK_GPIO,
	.task_stack_size = 8192,
	.task_priority = 5,
};

extern "C" void app_main() {
	ESP_ERROR_CHECK(app_network_init_and_connect());
	app_log_connected_ap_info();

	BaseType_t task_ok = xTaskCreate(
		microphone_uploader_task,
		"microphone_uploader_task",
		static_cast<uint32_t>(mic_uploader_config.task_stack_size),
		&mic_uploader_config,
		static_cast<UBaseType_t>(mic_uploader_config.task_priority),
		nullptr
	);

	if (task_ok != pdPASS) {
		ESP_LOGE(TAG, "Failed to create microphone uploader task");
		return;
	}

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
