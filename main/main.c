/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "tcpip_adapter.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"

#include "driver/gpio.h"
#include <esp_http_server.h>
#include <cJSON.h>
#include <driver/dac.h>

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

#define OUTPUT_PIN 18

static const char* TAG = "APP";

static void send_400_wrong_params(httpd_req_t* req) {
  ESP_LOGI(TAG, "Sending 400");
  httpd_resp_set_status(req, HTTPD_400);
  httpd_resp_sendstr(req, "Request wrong parameters");
}

static esp_err_t hello_get_handler(httpd_req_t* req) {
  static int level = 0;

  char buf[30];
  int status_query = httpd_req_get_url_query_str(req, buf, sizeof(buf));
  if (status_query == ESP_OK) {
    char param[4];
    httpd_query_key_value(buf, "vibration_level", param, sizeof(param));
    int param_level = atoi(param);
    if (level <= 255 && level >= 0)
      level = param_level;
    else {
      send_400_wrong_params(req);
      return ESP_OK;
    }
  } else if (status_query == ESP_ERR_NOT_FOUND) {
    level = level ? 0 : 255;
  } else {
    send_400_wrong_params(req);
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  cJSON* root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "vibration_level", level);
  const char* body = cJSON_Print(root);

  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_sendstr(req, body);

  cJSON_Delete(root);

  gpio_set_level(OUTPUT_PIN, level > 0);
  dac_output_voltage(DAC_CHANNEL_1, level);

  ESP_LOGI(TAG, "Vibration at %i", level);
  return ESP_OK;
}

static const httpd_uri_t hello = {
        .uri       = "/hello",
        .method    = HTTP_GET,
        .handler   = hello_get_handler
};

static httpd_handle_t start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &hello);
    return server;
  }

  ESP_LOGI(TAG, "Error starting server!");
  return NULL;
}

static void stop_webserver(httpd_handle_t server) {
  // Stop the httpd server
  httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  httpd_handle_t* server = (httpd_handle_t*) arg;
  if (*server) {
    ESP_LOGI(TAG, "Stopping webserver");
    stop_webserver(*server);
    *server = NULL;
  }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data) {
  httpd_handle_t* server = (httpd_handle_t*) arg;
  if (*server == NULL) {
    ESP_LOGI(TAG, "Starting webserver");
    *server = start_webserver();
  }
}


void app_main(void) {
  static httpd_handle_t server = NULL;

  dac_output_enable(DAC_CHANNEL_1);
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = 1ULL << OUTPUT_PIN;
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  ESP_ERROR_CHECK(nvs_flash_init());
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
   * Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  ESP_ERROR_CHECK(example_connect());

  /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
   * and re-start it upon connection.
   */

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

  /* Start the server for the first time */
  server = start_webserver();
}
