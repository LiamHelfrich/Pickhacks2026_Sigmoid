idf_component_register(SRCS "microphone_uploader.cpp" "network_rest.cpp" "main.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_http_client esp_netif esp-tls esp_wifi protocol_examples_common nvs_flash)