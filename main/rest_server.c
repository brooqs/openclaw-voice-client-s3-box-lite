#include "rest_server.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "REST_API";

// These globals replace the old UPnP ones
volatile bool rest_new_url_ready = false;
char rest_current_url[512] = {0};

static esp_err_t api_play_post_handler(httpd_req_t *req) {
  char buf[1024];
  int ret, remaining = req->content_len;

  if (remaining >= sizeof(buf)) {
    ESP_LOGE(TAG, "Payload too large");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int received = 0;
  while (remaining > 0) {
    if ((ret = httpd_req_recv(req, buf + received,
                              MIN(remaining, sizeof(buf) - received - 1))) <=
        0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(req);
      }
      return ESP_FAIL;
    }
    received += ret;
    remaining -= ret;
  }
  buf[received] = '\0';

  ESP_LOGI(TAG, "REST Payload: %s", buf);

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    ESP_LOGE(TAG, "Invalid JSON payload");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON payload");
    return ESP_FAIL;
  }

  cJSON *url_item = cJSON_GetObjectItem(json, "url");
  if (url_item && cJSON_IsString(url_item)) {
    strncpy(rest_current_url, url_item->valuestring,
            sizeof(rest_current_url) - 1);
    rest_current_url[sizeof(rest_current_url) - 1] = '\0';
    rest_new_url_ready = true;
    ESP_LOGI(TAG, "Parsed URL: %s", rest_current_url);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  } else {
    ESP_LOGE(TAG, "Missing or invalid 'url' in JSON");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' parameter");
  }

  cJSON_Delete(json);
  return ESP_OK;
}

static const httpd_uri_t api_play_uri = {.uri = "/api/play",
                                         .method = HTTP_POST,
                                         .handler = api_play_post_handler,
                                         .user_ctx = NULL};

static esp_err_t dummy_description_get_handler(httpd_req_t *req) {
  // Gracefully absorb lingering UPnP SSDP probes from router caches
  // (e.g. Windows Cast-to-Device) to prevent 404 log spam.
  httpd_resp_set_type(req, "text/xml");
  httpd_resp_send(req, "<?xml version=\"1.0\"?><root></root>",
                  HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static const httpd_uri_t dummy_description_uri = {
    .uri = "/description.xml",
    .method = HTTP_GET,
    .handler = dummy_description_get_handler,
    .user_ctx = NULL};

void rest_server_start(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;

  ESP_LOGI(TAG, "Starting REST HTTP Server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &api_play_uri);
    httpd_register_uri_handler(server, &dummy_description_uri);
  } else {
    ESP_LOGE(TAG, "Error starting REST server!");
  }
}
