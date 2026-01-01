#include "mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "dht.h"

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t client = NULL;
static bool mqtt_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT connecting...");
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to broker");
            mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected from broker");
            mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
        default:
            ESP_LOGD(TAG, "MQTT Other event id:%d", event->event_id);
            break;
    }
}

esp_err_t mqtt_init(const char *broker_url)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_url,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(client);
}

bool mqtt_publish(const char *topic, const char *data)
{
    if (!mqtt_connected || !client) return false;
    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    return msg_id != -1;
}

// Funkcja do wysyłania danych
void mqtt_publish_dht(float temperature, float humidity)
{
    if (!mqtt_connected || !client) return;

    char payload[100];
    snprintf(payload, sizeof(payload), "{\"temperature\":%.1f, \"humidity\":%.1f}", temperature, humidity);

    int msg_id = esp_mqtt_client_publish(client, "esp32/dht22", payload, 0, 1, 0);
    ESP_LOGI(TAG, "Wysłano dane do MQTT, msg_id=%d, payload=%s", msg_id, payload);
}
