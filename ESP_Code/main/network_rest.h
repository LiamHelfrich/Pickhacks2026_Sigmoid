#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t app_network_init_and_connect();
void app_log_connected_ap_info();
esp_err_t send_binary_post(const char* url, const uint8_t* data, size_t data_len);
