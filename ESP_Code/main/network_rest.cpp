#include "network_rest.h"

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

static const char* TAG = "network_rest";

esp_err_t app_network_init_and_connect() {
	esp_err_t err = nvs_flash_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
		return err;
	}

	err = esp_netif_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
		return err;
	}

	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
		return err;
	}

	err = example_connect();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "example_connect failed: %s", esp_err_to_name(err));
		return err;
	}

	return ESP_OK;
}

void app_log_connected_ap_info() {
	wifi_ap_record_t ap_info = {};
	esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to read AP info: %s", esp_err_to_name(err));
		return;
	}

	ESP_LOGI(TAG, "--- Access Point Information ---");
	ESP_LOG_BUFFER_HEX("MAC Address", ap_info.bssid, sizeof(ap_info.bssid));
	ESP_LOG_BUFFER_CHAR("SSID", ap_info.ssid, sizeof(ap_info.ssid));
	ESP_LOGI(TAG, "Primary Channel: %d", ap_info.primary);
	ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
}

esp_err_t send_binary_post(const char* url, const uint8_t* data, size_t data_len) {
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
