#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "WIFI";
static bool wifi_connected = false;  // Dodana zmienna
static bool ap_active = false;      // Dodana zmienna dla trybu AP

#define DEFAULT_AP_SSID "ESP32_AP"
#define DEFAULT_AP_PASS "12345678"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_AP_MAX_CONN 4

/* ==============================
   EVENT HANDLER
   ============================== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Próba połączenia z WiFi...");
        esp_wifi_connect();
        wifi_connected = false;
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Rozłączono z WiFi. Powód: %d", event->reason);
        // Wyświetl szczegółowy powód rozłączenia
        switch(event->reason) {
            case WIFI_REASON_AUTH_EXPIRE:
                ESP_LOGW(TAG, "Autoryzacja wygasła");
                break;
            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGW(TAG, "Błąd autoryzacji - sprawdź hasło");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGW(TAG, "Nie znaleziono sieci - sprawdź nazwę sieci");
                break;
            case WIFI_REASON_ASSOC_FAIL:
                ESP_LOGW(TAG, "Błąd asocjacji");
                break;
            default:
                ESP_LOGW(TAG, "Inny błąd: %d", event->reason);
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Czekaj sekundę przed ponowną próbą
        esp_wifi_connect();
        wifi_connected = false;
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Połączono z WiFi! IP: " IPSTR ", Maska: " IPSTR ", Gateway: " IPSTR,
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw));
        
        // Pokaż siłę sygnału
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Siła sygnału (RSSI): %d dBm", ap_info.rssi);
        }
        wifi_connected = true;
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* ev = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Client connected: MAC=" MACSTR ", AID=%d",
                 MAC2STR(ev->mac), ev->aid);
        ap_active = true;  // Zaktualizowany status AP
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* ev = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Client disconnected: MAC=" MACSTR ", AID=%d",
                 MAC2STR(ev->mac), ev->aid);
        // Nie zmieniamy ap_active, bo AP nadal działa
    }
}

/* ==============================
   INIT STA MODE (Client)
   ============================== */
esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    // Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    // Dodatkowa konfiguracja
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Ustaw typ autoryzacji
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Konfiguracja WiFi:");
    ESP_LOGI(TAG, "SSID: %s", ssid);
    ESP_LOGI(TAG, "Długość hasła: %d znaków", strlen(password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Łączenie z siecią WiFi...");
    return ESP_OK;
}

/* ==============================
   INIT ACCESS POINT MODE
   ============================== */
esp_err_t wifi_init_softap(void)
{
    // Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = DEFAULT_AP_SSID,
            .ssid_len = strlen(DEFAULT_AP_SSID),
            .channel = DEFAULT_AP_CHANNEL,
            .password = DEFAULT_AP_PASS,
            .max_connection = DEFAULT_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(DEFAULT_AP_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP uruchomiony! SSID: %s, hasło: %s", 
             DEFAULT_AP_SSID, DEFAULT_AP_PASS);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return wifi_connected || ap_active;
}
