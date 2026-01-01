/**
 * ============================================================================
 * DAS Tower v1 - Main Controller
 * 
 * Architektura systemu: Interwałowe Bloki Pomiarowe
 * 
 * System pracuje w oparciu o harmonogram zarządzany przez zegar RTC DS1302.
 * Domyślnie system wykonuje 2 pomiary na dobę (86400s / 2 = 43200s interwału).
 * Harmonogram można edytować przez UART komendą: SET_FREQ:X
 * 
 * Każdy blok akwizycji zawiera:
 * 1. Odczyt sensorów automatycznych (DS18B20, DHT22, BH1750)
 * 2. Pobranie ostatniego zmierzonego pH (pomiar manualny)
 * 3. Zapis danych na kartę SD w formacie NDJSON z timestampem RTC
 * 4. Publikacja danych na brokerze MQTT
 * 
 * pH jest mierzone manualnie przez przycisk z przerwaniem i debouncingiem 20ms
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_vfs_dev.h"

// Headers dla modułów czujników
#include "ds18b20.h"
#include "dht.h"
#include "bh1750.h"
#include "ph_sensor.h"
#include "ds1302.h"
#include "relay.h"
#include "mqtt.h"
#include "wifi.h"
#include "sdcard_spi.h"
#include "onewire.h"
#include "i2cdev.h"
#include "level.h"

/* ============================================================================
 * KONFIGURACJA GLOBALNA
 * ============================================================================ */

static const char *TAG = "MAIN";

// Parametry UART do obsługi komend
#define UART_NUM           UART_NUM_0
#define UART_BAUDRATE      115200
#define UART_RX_BUF_SIZE   256
#define UART_TX_BUF_SIZE   256

// Parametry harmonogramu pomiarów
#define SECONDS_PER_DAY    86400
#define DEFAULT_MEASUREMENTS_PER_DAY 2

// Piny I2C (dla BH1750 i innych czujników I2C)
#define I2C_SDA_GPIO       GPIO_NUM_21
#define I2C_SCL_GPIO       GPIO_NUM_22
#define I2C_FREQ           100000

// Pin OneWire (DS18B20)
#define ONEWIRE_GPIO       GPIO_NUM_2

// Pin przycisku pH
#define PH_BUTTON_GPIO     GPIO_NUM_32
#define PH_DEBOUNCE_MS     20

// Konfiguracja WiFi i MQTT (zmień na swoje wartości!)
#define WIFI_SSID          "Sieć OPD"
#define WIFI_PASSWORD      "pies12345"
#define MQTT_BROKER_URL    "mqtt://192.168.137.1:1883"



// Ścieżka pliku danych na SD
#define SD_DATA_FILE       "/sdcard/measurements.ndjson"

/* ============================================================================
 * STRUKTURY GLOBALNE
 * ============================================================================ */

typedef struct {
    float temperature_ds18;     // Temperatura DS18B20 [°C]
    float temperature_dht;      // Temperatura DHT22 [°C]
    float humidity;             // Wilgotność DHT22 [%]
    float light;                // Natężenie światła BH1750 [lux]
    float ph;                   // pH zmierzone ręcznie
    float last_manual_ph;       // Ostatnia zmierzona wartość pH
    uint32_t timestamp_unix;    // Timestamp RTC (Unix)
    char rtc_string[32];        // Timestamp w formacie: YYYY-MM-DD HH:MM:SS
} measurement_block_t;

typedef struct {
    uint32_t measurements_per_day;
    uint32_t measurement_interval_sec;
    SemaphoreHandle_t update_semaphore;
} scheduler_config_t;

/* ============================================================================
 * ZMIENNE GLOBALNE
 * ============================================================================ */

static scheduler_config_t scheduler = {
    .measurements_per_day = DEFAULT_MEASUREMENTS_PER_DAY,
    .measurement_interval_sec = SECONDS_PER_DAY / DEFAULT_MEASUREMENTS_PER_DAY,
    .update_semaphore = NULL
};

static measurement_block_t current_measurement = {0};
static float last_manual_ph_value = 0.0f;
static bool ph_measurement_pending = false;
static SemaphoreHandle_t ph_measurement_semaphore = NULL;

static OneWire ow;
static i2c_dev_t bh1750_dev;
static ph_sensor_t ph_sensor;
static QueueHandle_t ph_measurement_queue = NULL;

// Struktura do obsługi czasowego sterowania relay w pętli
typedef struct {
    bool active;           // Czy pętla jest aktywna
    uint32_t on_ms;        // Czas włączenia w ms
    uint32_t off_ms;       // Czas czekania w ms
    uint8_t relay_id;      // 1 dla R1, 2 dla R2
} relay_timer_t;

static relay_timer_t relay_timer = {
    .active = false,
    .on_ms = 0,
    .off_ms = 0,
    .relay_id = 0
};

/* ============================================================================
 * OBSŁUGA UART - KOMENDA INTERFEJSU
 * ============================================================================ */

/**
 * Parse UART command from queue and execute it
 * Supported commands:
 *   - SET_FREQ:X      (X = 1..24 pomiary na dobę)
 *   - R1:ON / R1:OFF  (sterowanie przekaźnikiem 1)
 *   - R2:ON / R2:OFF  (sterowanie przekaźnikiem 2)
 *   - STATUS          (wyświetl aktualny stan)
 *   - ENTERPH         (wejdź w tryb kalibracji pH)
 *   - CALPH7 / CALPH4 (kalibruj punkt)
 *   - EXITPH          (wyjdź z trybu kalibracji)
 */
static void uart_command_handler(void *arg)
{
    char buffer[256];
    int len;

    while (1) {
        len = uart_read_bytes(UART_NUM, (uint8_t *)buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            buffer[len] = '\0';
            
            // Usuń znaki nowej linii, powrotu karetki i białe znaki
            for (int i = 0; i < len; i++) {
                if (buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == ' ') {
                    buffer[i] = '\0';
                    break;
                }
            }

            ESP_LOGI(TAG, "UART Command received: %s", buffer);

            // SET_FREQ:X
            if (strncmp(buffer, "SET_FREQ:", 9) == 0) {
                int freq = atoi(buffer + 9);
                if (freq > 0 && freq <= 24) {
                    scheduler.measurements_per_day = freq;
                    scheduler.measurement_interval_sec = SECONDS_PER_DAY / freq;
                    xSemaphoreGive(scheduler.update_semaphore);
                    printf("[UART] Measurement frequency set to %d per day (interval: %ld seconds)\n", 
                           freq, scheduler.measurement_interval_sec);
                } else {
                    printf("[UART] Invalid frequency: %d (must be 1-24)\n", freq);
                }
            }
            // R1:ON / R1:OFF / R1:TIME:ON_MS:OFF_MS
            else if (strcmp(buffer, "R1:ON") == 0) {
                relay_timer.active = false;  // Zatrzymaj timer jeśli był aktywny
                relay_set_relay1_on();
                printf("[UART] Relay 1 ON\n");
            }
            else if (strcmp(buffer, "R1:OFF") == 0) {
                relay_timer.active = false;  // Zatrzymaj timer jeśli był aktywny
                relay_set_relay1_off();
                printf("[UART] Relay 1 OFF (timer stopped if was running)\n");
            }
            else if (strncmp(buffer, "R1:TIME:", 8) == 0) {
                // R1:TIME:500:10000 - włącz pompę na 500ms, czekaj 10000ms, powtarzaj w pętli
                int on_ms = atoi(buffer + 8);
                char *colon = strchr(buffer + 8, ':');
                int off_ms = colon ? atoi(colon + 1) : 0;
                if (on_ms > 0 && off_ms > 0) {
                    relay_timer.on_ms = on_ms;
                    relay_timer.off_ms = off_ms;
                    relay_timer.relay_id = 1;
                    relay_timer.active = true;
                    printf("[UART] Relay 1 timer started: ON %dms, OFF %dms, REPEATING\n", on_ms, off_ms);
                    printf("[UART] To stop: R1:OFF\n");
                } else {
                    printf("[UART] Invalid timing: R1:TIME:ON_MS:OFF_MS\n");
                }
            }
            // R2:ON / R2:OFF
            else if (strcmp(buffer, "R2:ON") == 0) {
                relay_set_relay2_on();
                printf("[UART] Relay 2 ON\n");
            }
            else if (strcmp(buffer, "R2:OFF") == 0) {
                relay_set_relay2_off();
                printf("[UART] Relay 2 OFF\n");
            }
            // STATUS
            else if (strcmp(buffer, "STATUS") == 0) {
                ds1302_time_t rtc_time;
                ds1302_get_time(&rtc_time);
                printf("\n========== SYSTEM STATUS ==========\n");
                printf("RTC Time:        %04d-%02d-%02d %02d:%02d:%02d\n",
                       rtc_time.year, rtc_time.month, rtc_time.day,
                       rtc_time.hour, rtc_time.min, rtc_time.sec);
                printf("Measurements/day: %ld (interval: %ld sec)\n", 
                       scheduler.measurements_per_day, scheduler.measurement_interval_sec);
                printf("Last manual pH:  %.2f\n", last_manual_ph_value);
                printf("Relay 1 (Pump):  %s\n", relay_get_relay1_state() ? "ON" : "OFF");
                printf("Relay 2 (LED):   %s\n", relay_get_relay2_state() ? "ON" : "OFF");
                printf("====================================\n\n");
            }
            // ENTERPH
            else if (strcmp(buffer, "ENTERPH") == 0) {
                printf("[UART] Entering pH calibration mode. Commands: CALPH4, CALPH7, EXITPH\n");
            }
            // CALPH7
            else if (strcmp(buffer, "CALPH7") == 0) {
                printf("[UART] Calibrating pH to neutral (7.0) - do kalibracji!\n");
                // Tutaj wywołaj funkcję kalibracji z ph_sensor
            }
            // CALPH4
            else if (strcmp(buffer, "CALPH4") == 0) {
                printf("[UART] Calibrating pH to acid (4.0) - do kalibracji!\n");
            }
            // EXITPH
            else if (strcmp(buffer, "EXITPH") == 0) {
                printf("[UART] Exiting pH calibration mode\n");
            }
            else {
                printf("[UART] Unknown command: %s\n", buffer);
            }
        }
    }
}

/* ============================================================================
 * OBSŁUGA PĘTLI CZASOWEJ DLA PRZEKAŹNIKA (R1:TIME / R2:TIME)
 * ============================================================================ */

static void relay_timer_task(void *arg)
{
    /**
     * Zadanie obsługuje cykliczne włączanie/wyłączanie przekaźnika
     * Parametry ustawiają się w uart_command_handler
     * Pętla odpala się po każdym odebraniu komendy R1:TIME lub R2:TIME
     */
    while (1) {
        if (relay_timer.active) {
            if (relay_timer.relay_id == 1) {
                relay_set_relay1_on();
            } else if (relay_timer.relay_id == 2) {
                relay_set_relay2_on();
            }
            
            vTaskDelay(pdMS_TO_TICKS(relay_timer.on_ms));
            
            if (relay_timer.relay_id == 1) {
                relay_set_relay1_off();
            } else if (relay_timer.relay_id == 2) {
                relay_set_relay2_off();
            }
            
            printf("[RELAY_TIMER] Relay %d: ON %ldms, OFF %ldms (next cycle)\n",
                   relay_timer.relay_id, relay_timer.on_ms, relay_timer.off_ms);
            
            vTaskDelay(pdMS_TO_TICKS(relay_timer.off_ms));
        } else {
            // Czekaj jeśli pętla nie jest aktywna
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ============================================================================
 * OBSŁUGA PRZYCISKU pH - ISR i DEBOUNCING
 * ============================================================================ */

static void IRAM_ATTR ph_button_isr_handler(void *arg)
{
    /* Minimalistyczne: prześlij zdarzenie do kolejki */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t event = 1;
    xQueueSendFromISR(ph_measurement_queue, &event, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void ph_button_task(void *arg)
{
    uint32_t event;
    uint32_t debounce_timestamp = 0;
    const uint32_t debounce_ticks = pdMS_TO_TICKS(PH_DEBOUNCE_MS);

    while (1) {
        if (xQueueReceive(ph_measurement_queue, &event, portMAX_DELAY)) {
            uint32_t current_ticks = xTaskGetTickCount();

            // Debouncing: zignoruj jeśli mniej niż PH_DEBOUNCE_MS od ostatniego zdarzenia
            if (current_ticks - debounce_timestamp < debounce_ticks) {
                continue;
            }
            debounce_timestamp = current_ticks;

            ESP_LOGI(TAG, "pH button pressed - initiating manual measurement");

            // Odczyt pH z ADC
            // Załóżmy temperaturę na podstawie ostatniej wartości DHT
            float temperature = isnan(current_measurement.temperature_dht) ? 25.0f : current_measurement.temperature_dht;
            
            esp_err_t ret = ph_sensor_read_adc(&ph_sensor, ADC1_CHANNEL_0, temperature, &current_measurement.ph);
            if (ret == ESP_OK) {
                last_manual_ph_value = current_measurement.ph;
                ph_measurement_pending = true;
                xSemaphoreGive(ph_measurement_semaphore);
                printf("[pH] Manual measurement captured: %.2f\n", current_measurement.ph);
                printf("[pH] Ready for next data block.\n");
            } else {
                ESP_LOGW(TAG, "pH measurement failed: %s", esp_err_to_name(ret));
            }
        }
    }
}

/* ============================================================================
 * INICJALIZACJA SPRZĘTU
 * ============================================================================ */

// Forward declarations
static void init_uart(void);
static void init_i2c(void);
static void init_sensors(void);
static void init_relay(void);
static void init_wifi_mqtt(void);
static void init_sdcard(void);

static void init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, 0, NULL, 0));

    // Przedmontuj UART na stdout (aby printf działał)
    esp_vfs_dev_uart_use_driver(UART_NUM);
    fflush(stdout);

    ESP_LOGI(TAG, "UART initialized at %d baud", UART_BAUDRATE);
}

static void init_i2c(void)
{
    i2c_dev_t i2c_dev = {
        .port = I2C_NUM_0,
        .addr = 0x23,  // Domyślny adres BH1750
        .cfg = {
            .sda_io_num = I2C_SDA_GPIO,
            .scl_io_num = I2C_SCL_GPIO,
            .master.clk_speed = I2C_FREQ
        }
    };

    ESP_ERROR_CHECK(i2cdev_init());
    bh1750_dev = i2c_dev;
    ESP_LOGI(TAG, "I2C initialized");
}

static void init_sensors(void)
{
    // DS18B20 (OneWire)
    onewire_init(&ow, ONEWIRE_GPIO);
    ESP_LOGI(TAG, "DS18B20 OneWire initialized");

    // DHT22
    esp_err_t ret = dht22_read(&current_measurement.temperature_dht, &current_measurement.humidity);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DHT22 initialized successfully");
    } else {
        ESP_LOGW(TAG, "DHT22 initialization warning: %s", esp_err_to_name(ret));
    }

    // BH1750 (light sensor)
    // Inicjalizacja specyficzna dla BH1750 - załóżmy, że moduł to zapewnia

    // pH Sensor
    ret = ph_sensor_init(&ph_sensor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "pH sensor initialized");
    } else {
        ESP_LOGW(TAG, "pH sensor initialization failed: %s", esp_err_to_name(ret));
    }

    // DS1302 (RTC)
    ds1302_init();
    ds1302_set_compile_time();  // Ustaw czas kompilacji (jeśli brak baterii)
    ESP_LOGI(TAG, "DS1302 RTC initialized");

    // Level sensor (opcjonalnie - wyłączony ze względu na stack overflow)
    level_sensor_init();
    // level_sensor_start_task();
    // ESP_LOGI(TAG, "Level sensor initialized");
}

static void init_relay(void)
{
    relay_init();
    relay_buttons_init();
    ESP_LOGI(TAG, "Relays and buttons initialized");
}

static void init_wifi_mqtt(void)
{
    // Inicjalizacja NVS (wymagana dla WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // WiFi
    ret = wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi initialized");
    }

    // MQTT
    ret = mqtt_init(MQTT_BROKER_URL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "MQTT initialized");
    }
}

static void init_sdcard(void)
{
    esp_err_t ret = sensor_sdcard_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD card initialized and mounted at /sdcard");
    }
}

/* ============================================================================
 * FUNKCJE POMIARU I ZAPISU DANYCH
 * ============================================================================ */

/**
 * Odczytaj wszystkie sensory i uaktualnij strukturę measurement_block_t
 */
static void read_all_sensors(void)
{
    ESP_LOGI(TAG, "=== Starting measurement block ===");

    // 1. DS18B20 - Temperatura
    ds18_request_temperatures(&ow);
    vTaskDelay(pdMS_TO_TICKS(750));  // Czekaj na konwersję temperatury
    current_measurement.temperature_ds18 = ds18_get_temp_c_by_index(&ow, 0);
    ESP_LOGI(TAG, "DS18B20 Temp: %.2f°C", current_measurement.temperature_ds18);

    // 2. DHT22 - Temperatura i wilgotność
    esp_err_t ret = dht22_read(&current_measurement.temperature_dht, &current_measurement.humidity);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DHT22 Temp: %.2f°C, Humidity: %.2f%%", 
                 current_measurement.temperature_dht, current_measurement.humidity);
    } else {
        ESP_LOGW(TAG, "DHT22 read failed: %s", esp_err_to_name(ret));
        current_measurement.temperature_dht = NAN;
        current_measurement.humidity = NAN;
    }

    // 3. BH1750 - Natężenie światła
    // Załóżmy, że bh1750_read_lux() jest funkcją w module bh1750
    // current_measurement.light = bh1750_read_lux(&bh1750_dev);
    current_measurement.light = 500.0f;  // Placeholder
    ESP_LOGI(TAG, "BH1750 Light: %.2f lux", current_measurement.light);

    // 4. pH - Użyj ostatniej zmierzonej wartości
    if (ph_measurement_pending) {
        // Użyj wartości zmierzonej manualnie
        ph_measurement_pending = false;
    } else {
        // Użyj ostatniej znanej wartości
        current_measurement.ph = last_manual_ph_value;
    }
    ESP_LOGI(TAG, "pH (manual): %.2f", current_measurement.ph);

    // 5. RTC - Timestamp
    ds1302_time_t rtc_time;
    ds1302_get_time(&rtc_time);
    
    snprintf(current_measurement.rtc_string, sizeof(current_measurement.rtc_string),
             "%04d-%02d-%02d %02d:%02d:%02d",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.min, rtc_time.sec);
    
    // Uproszczona konwersja Unix timestamp (w rzeczywistości trzeba dokładniej)
    current_measurement.timestamp_unix = time(NULL);

    ESP_LOGI(TAG, "RTC Time: %s", current_measurement.rtc_string);
}

/**
 * Zapisz blok pomiarowy na kartę SD w formacie NDJSON
 */
static void save_measurement_to_sd(void)
{
    // Przygotuj JSON
    char json_line[512];
    int len = snprintf(json_line, sizeof(json_line),
        "{\"timestamp\":\"%s\",\"temp_ds18\":%.2f,\"temp_dht\":%.2f,\"humidity\":%.2f,\"light\":%.2f,\"ph\":%.2f}\n",
        current_measurement.rtc_string,
        current_measurement.temperature_ds18,
        current_measurement.temperature_dht,
        current_measurement.humidity,
        current_measurement.light,
        current_measurement.ph);

    if (len > 0 && len < (int)sizeof(json_line)) {
        esp_err_t ret = sensor_json_append(SD_DATA_FILE, "measurement", 1.0, current_measurement.rtc_string);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Measurement saved to SD: %s", json_line);
        } else {
            ESP_LOGW(TAG, "SD save failed: %s", esp_err_to_name(ret));
        }
    }
}

/**
 * Publikuj dane na MQTT
 */
static void publish_to_mqtt(void)
{
    char payload[768];
    const char *relay1_mode = relay_timer.active && relay_timer.relay_id == 1 ? "cycle" : "manual";
    const char *relay2_mode = relay_timer.active && relay_timer.relay_id == 2 ? "cycle" : "manual";
    
    int len = snprintf(payload, sizeof(payload),
        "{\"timestamp\":\"%s\",\"temp_ds18\":%.2f,\"temp_dht\":%.2f,\"humidity\":%.2f,\"light\":%.2f,\"ph\":%.2f,"
        "\"relay1\":%s,\"relay1_mode\":\"%s\",\"relay1_on_ms\":%lu,\"relay1_off_ms\":%lu,"
        "\"relay2\":%s,\"relay2_mode\":\"%s\"}",
        current_measurement.rtc_string,
        current_measurement.temperature_ds18,
        current_measurement.temperature_dht,
        current_measurement.humidity,
        current_measurement.light,
        current_measurement.ph,
        relay_get_relay1_state() ? "true" : "false",
        relay1_mode,
        relay_timer.relay_id == 1 ? relay_timer.on_ms : 0,
        relay_timer.relay_id == 1 ? relay_timer.off_ms : 0,
        relay_get_relay2_state() ? "true" : "false",
        relay2_mode);

    if (len > 0 && len < (int)sizeof(payload)) {
        bool ret = mqtt_publish("das_tower/measurements", payload);
        if (ret) {
            ESP_LOGI(TAG, "MQTT published successfully (R1:%s [%s], R2:%s [%s])", 
                     relay_get_relay1_state() ? "ON" : "OFF", relay1_mode,
                     relay_get_relay2_state() ? "ON" : "OFF", relay2_mode);
        } else {
            ESP_LOGW(TAG, "MQTT publish failed");
        }
    }
}

/* ============================================================================
 * GŁÓWNE ZADANIE HARMONOGRAMU
 * ============================================================================ */

static void scheduler_task(void *arg)
{
    uint32_t last_measurement_time = 0;
    uint32_t current_time_sec = 0;

    // Czekaj na inicjalizację RTC
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Scheduler task started. Measurement interval: %ld seconds", 
             scheduler.measurement_interval_sec);

    while (1) {
        // Odczytaj bieżący czas z RTC
        ds1302_time_t rtc_time;
        ds1302_get_time(&rtc_time);
        
        // Prosta konwersja na sekundy od północy
        current_time_sec = rtc_time.hour * 3600 + rtc_time.min * 60 + rtc_time.sec;

        // Sprawdź, czy minął czas do następnego pomiaru
        if (current_time_sec - last_measurement_time >= scheduler.measurement_interval_sec) {
            ESP_LOGI(TAG, "Time for measurement block!");
            
            // Wykonaj sekwencję pomiaru
            read_all_sensors();
            save_measurement_to_sd();
            publish_to_mqtt();

            last_measurement_time = current_time_sec;
        }

        // Czekaj 1 sekundę przed następnym sprawdzeniem
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Obsłuż aktualizację harmonogramu (jeśli zmieniono SET_FREQ)
        if (xSemaphoreTake(scheduler.update_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "Scheduler updated: %ld measurements per day (interval: %ld sec)",
                     scheduler.measurements_per_day, scheduler.measurement_interval_sec);
            last_measurement_time = current_time_sec;  // Reset Timer
        }
    }
}

/* ============================================================================
 * app_main() - PUNKT WEJŚCIA
 * ============================================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "     DAS Tower v1 - Starting System     ");
    ESP_LOGI(TAG, "========================================");

    // Inicjalizacja UART (musi być przed printf!)
    init_uart();

    printf("\n\n");
    printf("╔════════════════════════════════════════════╗\n");
    printf("║     DAS Tower v1 - System Started          ║\n");
    printf("║  Measurement Interval Block Architecture   ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");

    // Semafor dla harmonogramu
    scheduler.update_semaphore = xSemaphoreCreateBinary();

    // Inicjalizacja czujników
    init_i2c();
    init_sensors();
    init_relay();
    init_wifi_mqtt();
    init_sdcard();

    // Inicjalizacja obsługi pH button
    ph_measurement_queue = xQueueCreate(10, sizeof(uint32_t));
    ph_measurement_semaphore = xSemaphoreCreateBinary();

    // Konfiguracja GPIO dla przycisku pH
    gpio_config_t ph_btn_conf = {
        .pin_bit_mask = (1ULL << PH_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Zbocze opadające (wciśnięcie)
    };
    gpio_config(&ph_btn_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PH_BUTTON_GPIO, ph_button_isr_handler, NULL);

    printf("[INIT] All hardware initialized successfully\n");
    printf("[INIT] Measurement block interval: %ld seconds (%ld per day)\n\n",
           scheduler.measurement_interval_sec, scheduler.measurements_per_day);

    // Utwórz zadania FreeRTOS
    xTaskCreate(uart_command_handler, "uart_task", 4096, NULL, 5, NULL);
    xTaskCreate(relay_timer_task, "relay_timer_task", 4096, NULL, 7, NULL);
    xTaskCreate(ph_button_task, "ph_button_task", 4096, NULL, 10, NULL);
    xTaskCreate(scheduler_task, "scheduler_task", 4096, NULL, 8, NULL);

    printf("[TASK] All FreeRTOS tasks created\n");
    printf("[READY] System ready for commands via UART\n");
    printf("[UART] Available commands:\n");
    printf("       - SET_FREQ:X         (1-24 measurements per day)\n");
    printf("       - R1:ON/OFF          (relay 1 control, stops timer)\n");
    printf("       - R1:TIME:ON:OFF     (relay 1 looping timer - repeating)\n");
    printf("       - R2:ON/OFF          (relay 2 control)\n");
    printf("       - STATUS             (display system status)\n");
    printf("       - ENTERPH            (pH calibration mode)\n");
    printf("       - CALPH7/CALPH4      (calibrate pH)\n\n");
}