idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_http_client esp_netif esp-tls esp_wifi protocol_examples_common nvs_flash)