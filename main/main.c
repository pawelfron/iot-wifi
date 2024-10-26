#include <stdio.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_http_client.h>

#define SSID "OnePlus 8T"
#define PASS "d2j8v5ny"
#define WIFI_SUCCESS 1
#define WIFI_FAILURE 2
#define LED_GPIO 2
#define URL "http://website.fis.agh.edu.pl"

static EventGroupHandle_t wifi_event_group;
static TaskHandle_t blinking_task_handle = NULL;
static bool wifi_connected = false;

static void blinking_task(void* pvParameters) {
    while (true) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Łączenie z Access Point...\n");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
		printf("Łączenie z Access Point...\n");

        if (blinking_task_handle == NULL) {
            xTaskCreate(blinking_task, "blink", 1024, NULL, 1, &blinking_task_handle);
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("Uzyskany adres IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);

        if (blinking_task_handle != NULL) {
            vTaskDelete(blinking_task_handle);
            blinking_task_handle = NULL;
            gpio_set_level(LED_GPIO, 0);
        }
    }
}

static esp_err_t http_request_handler(esp_http_client_event_handle_t event) {
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        printf("%.*s\n", event->data_len, (char *) event->data);
    }
    return ESP_OK;
}

void app_main(void) {
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();
    esp_event_handler_instance_t wifi_handler_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_handler_event_instance));
    esp_event_handler_instance_t got_ip_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &got_ip_event_instance));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASS,
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_SUCCESS | WIFI_FAILURE, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_SUCCESS) {
        printf("Połączono z Access Point.\n");
        wifi_connected = true;
    } else {
        wifi_connected = false;
        printf("Nie udało się połączyć z Access Point.\n");
    }

    esp_http_client_config_t http_request_config = {
        .url = URL,
        .method = HTTP_METHOD_GET,
        .cert_pem = NULL,
        .event_handler = http_request_handler
    };

    if (wifi_connected) {
        esp_http_client_handle_t client = esp_http_client_init(&http_request_config);
        esp_http_client_perform(client);
        esp_http_client_cleanup(client);
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_event_instance));   
    vEventGroupDelete(wifi_event_group);
    printf("Koniec.\n");
}
