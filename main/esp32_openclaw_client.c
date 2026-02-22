#include "bsp/esp-bsp.h"
#include "decoder/impl/esp_mp3_dec.h"
#include "driver/i2s_std.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "jarvis_image.h" // Needed for LCD Avatar
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "esp_http_server.h" // Needed for HTTPD types in rest_server.h
#include "rest_server.h"
#include "sdkconfig.h"
#include <arpa/inet.h>
#include <sys/socket.h>

// Define Wi-Fi credentials via menuconfig
#define WIFI_SSID CONFIG_OPENCLAW_WIFI_SSID
#define WIFI_PASS CONFIG_OPENCLAW_WIFI_PASSWORD

#define OPENCLAW_STT_URL CONFIG_OPENCLAW_ENDPOINT_URL

// Global flags from rest_server.c
extern volatile bool rest_new_url_ready;
extern char rest_current_url[512];

static const char *TAG = "openclaw_client";

static esp_codec_dev_handle_t spk_codec_dev;
static esp_codec_dev_handle_t mic_codec_dev;

static volatile bool is_recording = false;
static uint8_t *audio_buffer = NULL;
static size_t audio_buffer_len = 0;
#define MAX_AUDIO_PAYLOAD_SIZE (1024 * 1024)
static void button_press_down_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "PTT Button Pressed! Start recording audio...");
  is_recording = true;
}

static void button_press_up_cb(void *arg, void *usr_data) {
  ESP_LOGI(TAG, "PTT Button Released! Stop recording and send.");
  is_recording = false;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "retry to connect to the AP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
  }
}

void wifi_init_sta(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
}

static void send_audio_to_openclaw(uint8_t *audio_data, size_t len) {
  esp_http_client_config_t config = {
      .url = OPENCLAW_STT_URL,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 300000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  const char *boundary = "----OpenClawFormBoundaryESP32";
  char content_type[128];
  snprintf(content_type, sizeof(content_type),
           "multipart/form-data; boundary=%s", boundary);
  esp_http_client_set_header(client, "Content-Type", content_type);

  const char *part_header = "------OpenClawFormBoundaryESP32\r\n"
                            "Content-Disposition: form-data; name=\"audio\"; "
                            "filename=\"audio.raw\"\r\n"
                            "Content-Type: application/octet-stream\r\n\r\n";
  const char *part_footer = "\r\n------OpenClawFormBoundaryESP32--\r\n";

  int total_len = strlen(part_header) + len + strlen(part_footer);

  esp_err_t err = esp_http_client_open(client, total_len);
  if (err == ESP_OK) {
    esp_http_client_write(client, part_header, strlen(part_header));
    esp_http_client_write(client, (const char *)audio_data, len);
    esp_http_client_write(client, part_footer, strlen(part_footer));

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    bool is_chunked = esp_http_client_is_chunked_response(client);
    ESP_LOGI(TAG, "Jarvis Bridge HTTP POST Status = %d, Transfer-Encoding: %s",
             status_code, is_chunked ? "Chunked Stream" : "Fixed Length");

    // Read the MP3 Audio stream back and decode it to the speaker
    if (status_code == 200) {
      ESP_LOGI(
          TAG,
          "Audio Stream Received! Starting MP3 Decode to Internal Speaker...");
      esp_codec_dev_sample_info_t out_fs = {
          .bits_per_sample = 16,
          .channel = 1,
          .channel_mask = 0,
          .sample_rate = 16000,
          .mclk_multiple = 0,
      };
      esp_codec_dev_open(spk_codec_dev, &out_fs);

      esp_audio_dec_register_default();
      esp_audio_dec_handle_t mp3_decoder = NULL;
      esp_audio_dec_cfg_t dec_cfg = {
          .type = ESP_AUDIO_TYPE_MP3, .cfg = NULL, .cfg_sz = 0};
      esp_audio_dec_open(&dec_cfg, &mp3_decoder);

      if (mp3_decoder) {
        uint8_t *in_buf =
            heap_caps_malloc(8192, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        uint8_t *out_buf =
            heap_caps_malloc(8192, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);

        esp_audio_dec_in_raw_t raw = {.buffer = in_buf, .len = 0};
        esp_audio_dec_out_frame_t frame_out = {.buffer = out_buf, .len = 8192};

        while (1) {
          int to_read = 8192 - raw.len;

          int read_len =
              esp_http_client_read(client, (char *)in_buf + raw.len, to_read);

          if (read_len <= 0) {
            break;
          }

          raw.len += read_len;

          while (raw.len > 0) {
            esp_audio_err_t dec_err =
                esp_audio_dec_process(mp3_decoder, &raw, &frame_out);

            if (dec_err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
              out_buf = heap_caps_realloc(out_buf, frame_out.needed_size,
                                          MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
              if (!out_buf)
                break; // Memory error
              frame_out.buffer = out_buf;
              frame_out.len = frame_out.needed_size;
              continue; // try again with larger buffer
            }

            if (dec_err != ESP_AUDIO_ERR_OK) {
              break; // End decode block on failure or incomplete frame
            }

            esp_audio_dec_info_t dec_info;
            if (esp_audio_dec_get_info(mp3_decoder, &dec_info) ==
                ESP_AUDIO_ERR_OK) {
              if (dec_info.sample_rate != 0 &&
                  (dec_info.sample_rate != out_fs.sample_rate ||
                   dec_info.channel != out_fs.channel)) {
                out_fs.sample_rate = dec_info.sample_rate;
                out_fs.channel = dec_info.channel;
                esp_codec_dev_close(spk_codec_dev);
                esp_codec_dev_open(spk_codec_dev, &out_fs);

                ESP_LOGI(TAG,
                         "Reconfigured internal speaker to %luHz, %u channels",
                         (unsigned long)out_fs.sample_rate, out_fs.channel);
              }
            }

            if (frame_out.decoded_size > 0) {
              esp_codec_dev_write(spk_codec_dev, frame_out.buffer,
                                  frame_out.decoded_size);
            }

            if (raw.consumed > 0) {
              memmove(raw.buffer, raw.buffer + raw.consumed,
                      raw.len - raw.consumed);
              raw.len -= raw.consumed;
              raw.consumed = 0; // reset for next iteration
            } else {
              break; // Wait for more data
            }
          }
        }

        if (in_buf)
          heap_caps_free(in_buf);
        if (out_buf)
          heap_caps_free(out_buf);
        esp_audio_dec_close(mp3_decoder);
      } else {
        ESP_LOGE(TAG, "Failed to open MP3 Decoder handle");
      }
      esp_codec_dev_close(spk_codec_dev);
    } else {
      ESP_LOGI(TAG, "No valid audio response from server");
    }
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
}

static void audio_record_task(void *pvParameters) {
  esp_codec_dev_sample_info_t fs = {
      .bits_per_sample = 16,
      .channel = 1,
      .channel_mask = 0,
      .sample_rate = 16000,
      .mclk_multiple = 0,
  };

  esp_codec_dev_open(mic_codec_dev, &fs);

  uint8_t chunk_buf[1024];

  while (1) {
    if (is_recording) {
      if (audio_buffer == NULL) {
        audio_buffer = heap_caps_malloc(MAX_AUDIO_PAYLOAD_SIZE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        audio_buffer_len = 0;
        if (audio_buffer == NULL) {
          ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM!");
          is_recording = false;
          continue;
        }
      }
      if (audio_buffer != NULL && audio_buffer_len < MAX_AUDIO_PAYLOAD_SIZE) {
        esp_err_t err =
            esp_codec_dev_read(mic_codec_dev, chunk_buf, sizeof(chunk_buf));
        if (err == ESP_OK) {
          int read_len = sizeof(chunk_buf);
          if (audio_buffer_len + read_len <= MAX_AUDIO_PAYLOAD_SIZE) {
            memcpy(audio_buffer + audio_buffer_len, chunk_buf, read_len);
            audio_buffer_len += read_len;
          }
        }
      }
    } else {
      if (audio_buffer != NULL && audio_buffer_len > 0) {
        bool has_sound = false;
        for (size_t i = 0; i < audio_buffer_len; i++) {
          if (audio_buffer[i] != 0x00 && audio_buffer[i] != 0xFF) {
            has_sound = true;
            break;
          }
        }

        ESP_LOGI(TAG,
                 "Recording finished, size: %zu bytes. Valid: %s. Sending to "
                 "OpenClaw...",
                 audio_buffer_len, has_sound ? "YES" : "NO (SILENT)");

        if (has_sound) {
          send_audio_to_openclaw(audio_buffer, audio_buffer_len);
        } else {
          ESP_LOGW(TAG, "Audio buffer empty or silent, skipping send!");
        }

        heap_caps_free(audio_buffer);
        audio_buffer = NULL;
        audio_buffer_len = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// Task to handle background MP3 stream playback specifically for Home Assistant
// REST
static void http_audio_task(void *pvParameters) {
  esp_err_t err;

  while (1) {
    if (rest_new_url_ready) {
      rest_new_url_ready = false;
      ESP_LOGI(TAG, "Starting REST HTTP Playback: %s", rest_current_url);

      esp_http_client_config_t config = {
          .url = rest_current_url,
          .timeout_ms = 300000,
          .buffer_size_tx = 1024,
          .buffer_size = 1024 * 8, // Increase receive buffer slightly inline
                                   // with UPnP chunk delays
      };

      esp_http_client_handle_t client = esp_http_client_init(&config);
      err = esp_http_client_open(client, 0);

      if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        if (status_code == 200 || status_code == 206) {
          ESP_LOGI(TAG, "REST Audio Stream Connected! Decoding...");
          esp_codec_dev_sample_info_t out_fs = {
              .bits_per_sample = 16,
              .channel = 1,
              .channel_mask = 0,
              .sample_rate = 16000,
              .mclk_multiple = 0,
          };
          esp_codec_dev_open(spk_codec_dev, &out_fs);

          esp_audio_dec_register_default();
          esp_audio_dec_handle_t mp3_decoder = NULL;
          esp_audio_dec_cfg_t dec_cfg = {
              .type = ESP_AUDIO_TYPE_MP3, .cfg = NULL, .cfg_sz = 0};

          if (esp_audio_dec_open(&dec_cfg, &mp3_decoder) == ESP_AUDIO_ERR_OK) {
            uint8_t *in_buf =
                heap_caps_malloc(8192, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
            uint8_t *out_buf =
                heap_caps_malloc(8192, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);

            esp_audio_dec_in_raw_t raw = {.buffer = in_buf, .len = 0};
            esp_audio_dec_out_frame_t frame_out = {.buffer = out_buf,
                                                   .len = 8192};

            while (!rest_new_url_ready) {
              int to_read = 8192 - raw.len;
              int read_len = esp_http_client_read(
                  client, (char *)in_buf + raw.len, to_read);

              if (read_len <= 0)
                break;
              raw.len += read_len;

              while (raw.len > 0) {
                esp_audio_err_t dec_err =
                    esp_audio_dec_process(mp3_decoder, &raw, &frame_out);

                if (dec_err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                  out_buf =
                      heap_caps_realloc(out_buf, frame_out.needed_size,
                                        MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                  if (!out_buf)
                    break;
                  frame_out.buffer = out_buf;
                  frame_out.len = frame_out.needed_size;
                  continue;
                }

                if (dec_err != ESP_AUDIO_ERR_OK)
                  break;

                esp_audio_dec_info_t dec_info;
                if (esp_audio_dec_get_info(mp3_decoder, &dec_info) ==
                    ESP_AUDIO_ERR_OK) {
                  if (dec_info.sample_rate != 0 &&
                      (dec_info.sample_rate != out_fs.sample_rate ||
                       dec_info.channel != out_fs.channel)) {
                    out_fs.sample_rate = dec_info.sample_rate;
                    out_fs.channel = dec_info.channel;
                    esp_codec_dev_close(spk_codec_dev);
                    esp_codec_dev_open(spk_codec_dev, &out_fs);
                  }
                }

                if (frame_out.decoded_size > 0) {
                  esp_codec_dev_write(spk_codec_dev, frame_out.buffer,
                                      frame_out.decoded_size);
                }

                if (raw.consumed > 0) {
                  memmove(raw.buffer, raw.buffer + raw.consumed,
                          raw.len - raw.consumed);
                  raw.len -= raw.consumed;
                  raw.consumed = 0;
                } else {
                  break;
                }
              }
            }

            if (in_buf)
              heap_caps_free(in_buf);
            if (out_buf)
              heap_caps_free(out_buf);
            esp_audio_dec_close(mp3_decoder);
          }
          esp_codec_dev_close(spk_codec_dev);
        } else {
          ESP_LOGE(TAG, "REST HTTP Error Code: %d", status_code);
        }
      } else {
        ESP_LOGE(TAG, "REST HTTP Open Failed: %s", esp_err_to_name(err));
      }
      esp_http_client_cleanup(client);
      ESP_LOGI(TAG, "REST HTTP Playback Stopped.");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Connect to WIFI
  ESP_LOGI(TAG, "Initialize Wi-Fi");
  wifi_init_sta();

  // Initialize SPI/I2C buses
  ESP_LOGI(TAG, "Initialize I2C bus...");
  ESP_ERROR_CHECK(bsp_i2c_init());

  // Initialize BSP Audio Input & Output
  ESP_LOGI(TAG, "Initializing Audio Codec...");
  ESP_ERROR_CHECK(bsp_audio_init(NULL));
  mic_codec_dev = bsp_audio_codec_microphone_init();
  spk_codec_dev = bsp_audio_codec_speaker_init();
  esp_codec_dev_set_in_gain(mic_codec_dev, 40.0);
  esp_codec_dev_set_out_vol(spk_codec_dev, 100);

  // Initialize LCD Display & Render Jarvis Avatar
  ESP_LOGI(TAG, "Initializing Display and Rendering Jarvis Avatar...");
  bsp_display_config_t disp_cfg = {.max_transfer_sz = 320 * 40 * 2};
  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_err_t disp_err = bsp_display_new(&disp_cfg, &panel_handle, &io_handle);
  if (disp_err == ESP_OK && panel_handle != NULL) {
    esp_lcd_panel_disp_on_off(panel_handle, true);
    bsp_display_backlight_on();

    // Render in chunks to respect ESP32 SPI DMA transfer limits (64KB chunks
    // max)
    for (int y = 0; y < 240; y += 40) {
      esp_lcd_panel_draw_bitmap(panel_handle, 0, y, 320, y + 40,
                                &jarvis_image_data[y * 320 * 2]);
      vTaskDelay(pdMS_TO_TICKS(5)); // Allow DMA frame to successfully flush
    }

    ESP_LOGI(TAG, "Jarvis Avatar successfully drawn to LCD.");
  } else {
    ESP_LOGE(TAG, "Failed to initialize the LCD Display!");
  }

  // Initialize PTT Button
  button_handle_t btns[BSP_BUTTON_NUM];
  int btn_cnt = 0;
  ESP_ERROR_CHECK(bsp_iot_button_create(btns, &btn_cnt, BSP_BUTTON_NUM));
  iot_button_register_cb(btns[0], BUTTON_PRESS_DOWN, button_press_down_cb,
                         NULL);
  iot_button_register_cb(btns[0], BUTTON_PRESS_UP, button_press_up_cb, NULL);

  xTaskCreatePinnedToCore(audio_record_task, "audio_record", 4096 * 2, NULL, 5,
                          NULL, 1);

  // Initialize Home Assistant REST Media API
  ESP_LOGI(TAG,
           "Initializing Native Home Assistant REST Server on Port 8080...");
  rest_server_start();
  xTaskCreate(http_audio_task, "http_audio", 8192, NULL, 5, NULL);

  ESP_LOGI(TAG, "Ready. Waiting for PTT Button press or HA REST Stream.");
  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
