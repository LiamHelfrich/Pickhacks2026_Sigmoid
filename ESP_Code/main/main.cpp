#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include <string.h>
#include <stdio.h>

#include "protocol_examples_common.h"
#include "esp_wifi.h"

#define TAG "simple_connect_example"

#include "rock_dove.h"

#define BLINK_GPIO GPIO_NUM_8

static esp_err_t send_binary_post(const char* url, const uint8_t* data, size_t data_len) {
	if (url == nullptr || data == nullptr || data_len == 0) {
		ESP_LOGE(TAG, "Upload args are invalid");
		return ESP_ERR_INVALID_ARG;
	}

	esp_http_client_config_t config = {};
	config.url = url;
	config.timeout_ms = 10000;

	esp_http_client_handle_t client = esp_http_client_init(&config);
	if (client == nullptr) {
		ESP_LOGE(TAG, "Failed to initialize HTTP client");
		return ESP_FAIL;
	}

	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_method(client, HTTP_METHOD_POST));
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_header(client, "Content-Type", "application/octet-stream"));
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_post_field(client, reinterpret_cast<const char*>(data), data_len));

	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		int status_code = esp_http_client_get_status_code(client);
		int64_t content_length = esp_http_client_get_content_length(client);
		ESP_LOGI(TAG, "Upload status=%d, content_length=%lld", status_code, content_length);
	} else {
		ESP_LOGE(TAG, "HTTP upload failed: %s", esp_err_to_name(err));
	}

	esp_http_client_cleanup(client);
	return err;
}

extern "C" void app_main() {
    // System initialization
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

	const char* endpoint = "http://66.42.127.17:5000/upload";
	// const uint8_t payload[] = {
	// 	0x45, 0x53, 0x50, 0x33, 0x32, 0x2D, 0x43, 0x33, 0x2D, 0x42, 0x49, 0x4E
	// };

    // Print out Access Point Information
	wifi_ap_record_t ap_info;
	ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
	ESP_LOGI(TAG, "--- Access Point Information ---");
	ESP_LOG_BUFFER_HEX("MAC Address", ap_info.bssid, sizeof(ap_info.bssid));
	ESP_LOG_BUFFER_CHAR("SSID", ap_info.ssid, sizeof(ap_info.ssid));
	ESP_LOGI(TAG, "Primary Channel: %d", ap_info.primary);
	ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
    
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
	
	while (true) {
		gpio_set_level(BLINK_GPIO, 1);
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		gpio_set_level(BLINK_GPIO, 0);
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		ESP_ERROR_CHECK_WITHOUT_ABORT(send_binary_post(endpoint, rock_dove_bin, sizeof(rock_dove_bin) / sizeof(rock_dove_bin[0])));

	}

	ESP_ERROR_CHECK(example_disconnect());
}
