#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <array>

#include "protocol_examples_common.h"
#include "esp_wifi.h"

#define TAG "simple_connect_example"

#define BLINK_GPIO GPIO_NUM_8

#define I2S_SEL_GPIO GPIO_NUM_7
#define I2S_LRCL_GPIO GPIO_NUM_6
#define I2S_DOUT_GPIO GPIO_NUM_4
#define I2S_BCLK_GPIO GPIO_NUM_5

static constexpr int I2S_SAMPLE_RATE_HZ = 8000;
static constexpr int AUDIO_UPLOAD_WINDOW_SEC = 10;
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

static esp_err_t init_i2s_mic() {
	gpio_reset_pin(I2S_SEL_GPIO);
	gpio_set_direction(I2S_SEL_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(I2S_SEL_GPIO, 0);

	i2s_config_t i2s_config = {};
	i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
	i2s_config.sample_rate = I2S_SAMPLE_RATE_HZ;
	i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
	i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
#ifdef I2S_COMM_FORMAT_STAND_I2S
	i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
	i2s_config.communication_format = I2S_COMM_FORMAT_I2S;
#endif
	i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
	i2s_config.dma_buf_count = 8;
	i2s_config.dma_buf_len = 512;
	i2s_config.use_apll = false;
	i2s_config.tx_desc_auto_clear = false;
	i2s_config.fixed_mclk = 0;

	i2s_pin_config_t pin_config = {};
	pin_config.bck_io_num = I2S_BCLK_GPIO;
	pin_config.ws_io_num = I2S_LRCL_GPIO;
	pin_config.data_out_num = I2S_PIN_NO_CHANGE;
	pin_config.data_in_num = I2S_DOUT_GPIO;

	esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
		return err;
	}

	err = i2s_set_pin(I2S_PORT, &pin_config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
		return err;
	}

	err = i2s_zero_dma_buffer(I2S_PORT);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2s_zero_dma_buffer failed: %s", esp_err_to_name(err));
		return err;
	}

	ESP_LOGI(TAG, "I2S microphone initialized");
	return ESP_OK;
}

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

static constexpr size_t bytes_per_upload = static_cast<size_t>(I2S_SAMPLE_RATE_HZ) * sizeof(int16_t) * AUDIO_UPLOAD_WINDOW_SEC;
static constexpr size_t read_chunk_size = 2048;
static std::array<uint8_t, bytes_per_upload> audio_buffer;

extern "C" void app_main() {
    // System initialization
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

	const char* endpoint = "http://66.42.127.17:5000/upload";

	ESP_ERROR_CHECK(init_i2s_mic());

	ESP_LOGI(TAG, "I2S microphone initialized");
	

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
		size_t total_bytes_read = 0;
		gpio_set_level(BLINK_GPIO, 1);

		ESP_LOGI(TAG, "Starting audio capture for %d seconds", AUDIO_UPLOAD_WINDOW_SEC);

		while (total_bytes_read < bytes_per_upload) {
			size_t bytes_read = 0;
			const size_t bytes_to_read = std::min(read_chunk_size, bytes_per_upload - total_bytes_read);

			esp_err_t read_err = i2s_read(
				I2S_PORT,
				audio_buffer.data() + total_bytes_read,
				bytes_to_read,
				&bytes_read,
				pdMS_TO_TICKS(1000)
			);

			if (read_err != ESP_OK) {
				ESP_LOGE(TAG, "i2s_read failed: %s", esp_err_to_name(read_err));
				continue;
			}

			if (bytes_read == 0) {
				ESP_LOGW(TAG, "i2s_read returned 0 bytes");
				continue;
			}

			total_bytes_read += bytes_read;
		}

		gpio_set_level(BLINK_GPIO, 0);
		ESP_LOGI(TAG, "Captured %u bytes, uploading", static_cast<unsigned>(total_bytes_read));
		ESP_ERROR_CHECK_WITHOUT_ABORT(send_binary_post(endpoint, audio_buffer.data(), total_bytes_read));

	}

	ESP_ERROR_CHECK(example_disconnect());
}
