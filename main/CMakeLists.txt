idf_component_register(SRCS "esp32_openclaw_client.c" "rest_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client esp_websocket_client json nvs_flash wifi_provisioning espressif__esp-box-lite esp_http_server lwip)
