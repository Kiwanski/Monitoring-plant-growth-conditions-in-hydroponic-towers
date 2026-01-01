#include "level.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LEVEL_SENSOR";

// ============== STRUKTURA STANU CZUJNIKA ==============
typedef struct {
    uint8_t current_state;           // Obecny stan (surowy odczyt)
    uint8_t debounce_state;          // Stan po deboucingu (0=pusty, 1=woda)
    uint32_t debounce_timer_ms;      // Timer debouncer
} level_sensor_t;

static level_sensor_t level_sensor = {
    .current_state = 0,              // Zaczynamy od stanu "brak wody" (pin LOW)
    .debounce_state = 0,
    .debounce_timer_ms = 0
};

// ============== INICJALIZACJA ==============
void level_sensor_init(void) {
    ESP_LOGI(TAG, "Inicjalizacja czujnika poziomów...");
    
    // Konfiguracja pinu GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LEVEL_SENSOR_PIN),  // Wybór pinu
        .mode = GPIO_MODE_INPUT,                      // Tryb wejścia
        .pull_up_en = GPIO_PULLUP_ENABLE,            // Włączenie pull-up (3.3V w spoczynku)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,       // Wyłączenie pull-down
        .intr_type = GPIO_INTR_DISABLE               // Bez przerwań (polling)
    };
    
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Pin GPIO%d skonfigurowany z pull-up rezystorem", LEVEL_SENSOR_PIN);
}

// ============== ODCZYT I DEBOUNCING ==============
/**
 * Aktualizuj stan czujnika z deboucingiem
 * 
 * Logika (ZMIENIONA):
 * - 0 (LOW)  = Brak wody / pływak poniżej kontaktronu
 * - 1 (HIGH) = Jest woda / pływak przy kontaktronie
 * 
 * Debouncing: Stan zmienia się tylko po DEBOUNCE_TIME_MS ms stabilności
 */
static void level_sensor_update(void) {
    // Odczyt surowego stanu
    uint8_t raw_state = gpio_get_level(LEVEL_SENSOR_PIN);
    
    // Debouncing - licz czas zmienności
    if (raw_state != level_sensor.current_state) {
        // Wykryto zmianę - uruchom timer
        level_sensor.debounce_timer_ms += SENSOR_POLL_INTERVAL_MS;
        
        if (level_sensor.debounce_timer_ms >= DEBOUNCE_TIME_MS) {
            // Stan jest stabilny przez DEBOUNCE_TIME_MS - zaakceptuj zmianę
            level_sensor.current_state = raw_state;
            level_sensor.debounce_state = raw_state;
            level_sensor.debounce_timer_ms = 0;
            
            // Logowanie zmiany stanu
            if (raw_state == 1) {
                ESP_LOGW(TAG, "⚠️  WODA WYKRYTA (pin HIGH) - pływak dotknął kontaktronu");
            } else {
                ESP_LOGI(TAG, "✓ BRAK WODY (pin LOW) - pływak poniżej kontaktronu");
            }
        }
    } else {
        // Stan jest stabilny - resetuj timer
        level_sensor.debounce_timer_ms = 0;
    }
}

// ============== FUNKCJE ZWRACAJĄCE STAN ==============
uint8_t level_sensor_get_raw(void) {
    return gpio_get_level(LEVEL_SENSOR_PIN);
}

uint8_t level_sensor_get_debounced(void) {
    return level_sensor.debounce_state;
}

bool level_sensor_has_water(void) {
    return (level_sensor.debounce_state == 1);  // 1 = HIGH = jest woda
}

bool level_sensor_is_empty(void) {
    return (level_sensor.debounce_state == 0);  // 0 = LOW = brak wody
}

// ============== GŁÓWNA PĘTLA ZADANIA ==============
static void level_sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Czujnik poziomów uruchomiony");
    
    while (1) {
        // Aktualizuj stan z deboucingiem
        level_sensor_update();
        
        // Okresowy wydruk diagnostyki
        static uint32_t print_counter = 0;
        print_counter++;
        
        if (print_counter >= (2000 / SENSOR_POLL_INTERVAL_MS)) {  // Co 2 sekundy
            print_counter = 0;
            
            ESP_LOGI(TAG, "Stan: Raw=%d, Debounced=%d, Status=%s",
                level_sensor.current_state,
                level_sensor.debounce_state,
                level_sensor_has_water() ? "🌊 WODA" : "☐ PUSTY"
            );
        }
        
        // Czekaj przed następnym odczytem
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}

// ============== URUCHOMIENIE ZADANIA ==============
void level_sensor_start_task(void) {
    xTaskCreate(level_sensor_task, "level_sensor_task", 2048, NULL, 5, NULL);
}
