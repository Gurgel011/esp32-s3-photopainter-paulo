#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "display.h"

static const char *TAG = "photopainter";
static EventGroupHandle_t wifi_events;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY 8

static int wifi_retry_count;

extern const uint8_t antena1_logo_png_start[] asm("_binary_antena1_logo_png_start");
extern const uint8_t antena1_logo_png_end[] asm("_binary_antena1_logo_png_end");

static void log_device_identity(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const size_t logo_size = (size_t)(antena1_logo_png_end - antena1_logo_png_start);

    ESP_LOGI(TAG, "Projeto novo: ESP32-S3 PhotoPainter Paulo");
    ESP_LOGI(TAG, "Marca principal: %s", CONFIG_APP_DEVICE_TITLE);
    ESP_LOGI(TAG, "Mensagem: %s", CONFIG_APP_DEVICE_MESSAGE);
    ESP_LOGI(TAG, "Logo Antena 1 embutido: %u bytes", (unsigned)logo_size);
    ESP_LOGI(TAG, "MAC Wi-Fi STA: %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < WIFI_MAX_RETRY) {
            wifi_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi desconectado, tentando novamente (%d/%d)", wifi_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_events, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        wifi_retry_count = 0;
        ESP_LOGI(TAG, "Wi-Fi conectado, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_wifi_sta(void)
{
    if (strlen(CONFIG_APP_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "Wi-Fi nao configurado. Use idf.py menuconfig para definir SSID e senha.");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando ao Wi-Fi: %s", CONFIG_APP_WIFI_SSID);
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    log_device_identity();

    esp_err_t display_ret = display_show_antena1();
    if (display_ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar tela: %s", esp_err_to_name(display_ret));
    }

    esp_err_t wifi_ret = init_wifi_sta();
    if (wifi_ret == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(
            wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Base Wi-Fi pronta para as proximas etapas.");
        } else {
            ESP_LOGW(TAG, "Wi-Fi nao conectou dentro do tempo esperado.");
        }
    }

    while (true) {
        ESP_LOGI(TAG, "Firmware base ativo. Proxima etapa: exibir o logo da Antena 1 na tela.");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
