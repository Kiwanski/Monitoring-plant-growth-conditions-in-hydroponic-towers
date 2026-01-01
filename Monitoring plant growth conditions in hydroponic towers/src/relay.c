#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "relay.h"

static const char *TAG = "RELAY";

/* ================== STAN WEWNĘTRZNY ================== */

static volatile bool relay1_on = false;
static volatile bool relay2_on = false;
static QueueHandle_t gpio_evt_queue = NULL;

/* Konfiguracja trybu interwałowego */
static relay_interval_config_t relay1_interval = {0, 0, false};
static relay_interval_config_t relay2_interval = {0, 0, false};

/* Timery dla trybu interwałowego */
static TimerHandle_t relay1_interval_timer = NULL;
static TimerHandle_t relay2_interval_timer = NULL;

/* Liczniki dla stanów interwałowych */
static uint32_t relay1_interval_counter = 0;
static uint32_t relay2_interval_counter = 0;

/* ================== POMOCNICZE FUNKCJE ================== */

static inline int relay_level_for(bool on)
{
    /* Jeżeli przekaźnik jest active-LOW: ON -> 0, OFF -> 1
       Jeżeli active-HIGH: ON -> 1, OFF -> 0 */
    if (RELAY_ACTIVE_LOW) {
        return on ? 0 : 1;
    } else {
        return on ? 1 : 0;
    }
}

static void relay_apply_all(void)
{
    gpio_set_level(RELAY1_GPIO, relay_level_for(relay1_on));
    gpio_set_level(RELAY2_GPIO, relay_level_for(relay2_on));
}

static void relay_set(gpio_num_t pin, bool on)
{
    gpio_set_level(pin, relay_level_for(on));
}

/* ================== ISR PRZYCISKÓW ================== */

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    /* Krótko i bezpiecznie: prześlij numer pinu do kolejki */
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

/* ================== INICJALIZACJA ================== */

void relay_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY1_GPIO) | (1ULL << RELAY2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Ustaw stan bezpieczny (OFF) zanim podasz sygnały na moduł */
    relay1_on = false;
    relay2_on = false;
    relay_apply_all();

    ESP_LOGI(TAG, "Relays initialized on GPIO %d and %d (active-%s)",
             RELAY1_GPIO, RELAY2_GPIO, RELAY_ACTIVE_LOW ? "LOW" : "HIGH");
}

void relay_buttons_init(void)
{
    /* Utwórz kolejkę dla zdarzeń GPIO */
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!gpio_evt_queue) {
        ESP_LOGE(TAG, "Failed to create GPIO event queue!");
        return;
    }

    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON1_GPIO) | (1ULL << BUTTON2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     /* przycisk zwiera do GND */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE        /* zbocze opadające = wciśnięcie */
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    /* Zainstaluj wspólną usługę ISR */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON1_GPIO, gpio_isr_handler, (void*)BUTTON1_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON2_GPIO, gpio_isr_handler, (void*)BUTTON2_GPIO));

    ESP_LOGI(TAG, "Buttons initialized on GPIO %d and %d (pull-up, active-LOW press)",
             BUTTON1_GPIO, BUTTON2_GPIO);
}

/* ================== FUNKCJE STEROWANIA PRZEKAŹNIKAMI ================== */

void relay_toggle_relay1(void)
{
    relay1_on = !relay1_on;
    relay_set(RELAY1_GPIO, relay1_on);
    ESP_LOGI(TAG, "Relay 1: %s", relay1_on ? "ON" : "OFF");
}

void relay_toggle_relay2(void)
{
    relay2_on = !relay2_on;
    relay_set(RELAY2_GPIO, relay2_on);
    ESP_LOGI(TAG, "Relay 2: %s", relay2_on ? "ON" : "OFF");
}

void relay_set_relay1_on(void)
{
    if (!relay1_on) {
        relay1_on = true;
        relay_set(RELAY1_GPIO, relay1_on);
        ESP_LOGI(TAG, "Relay 1: ON");
    }
}

void relay_set_relay1_off(void)
{
    if (relay1_on) {
        relay1_on = false;
        relay_set(RELAY1_GPIO, relay1_on);
        ESP_LOGI(TAG, "Relay 1: OFF");
    }
}

void relay_set_relay2_on(void)
{
    if (!relay2_on) {
        relay2_on = true;
        relay_set(RELAY2_GPIO, relay2_on);
        ESP_LOGI(TAG, "Relay 2: ON");
    }
}

void relay_set_relay2_off(void)
{
    if (relay2_on) {
        relay2_on = false;
        relay_set(RELAY2_GPIO, relay2_on);
        ESP_LOGI(TAG, "Relay 2: OFF");
    }
}

bool relay_get_relay1_state(void)
{
    return relay1_on;
}

bool relay_get_relay2_state(void)
{
    return relay2_on;
}

void* relay_get_event_queue(void)
{
    return (void*)gpio_evt_queue;
}

/* ================== TRYB INTERWAŁOWY - CALLBACKI TIMERÓW ================== */

static void relay1_interval_timer_callback(TimerHandle_t xTimer)
{
    if (!relay1_interval.enabled) {
        return;
    }

    relay1_interval_counter += 100; /* Timer co 100ms */

    if (relay1_interval_counter < relay1_interval.on_time_ms) {
        /* Włącz relay */
        if (!relay1_on) {
            relay1_on = true;
            relay_set(RELAY1_GPIO, relay1_on);
        }
    } else if (relay1_interval_counter < relay1_interval.interval_ms) {
        /* Wyłącz relay */
        if (relay1_on) {
            relay1_on = false;
            relay_set(RELAY1_GPIO, relay1_on);
        }
    } else {
        /* Resetuj licznik na koniec cyklu */
        relay1_interval_counter = 0;
        ESP_LOGI(TAG, "Relay 1 interval cycle complete");
    }
}

static void relay2_interval_timer_callback(TimerHandle_t xTimer)
{
    if (!relay2_interval.enabled) {
        return;
    }

    relay2_interval_counter += 100; /* Timer co 100ms */

    if (relay2_interval_counter < relay2_interval.on_time_ms) {
        /* Włącz relay */
        if (!relay2_on) {
            relay2_on = true;
            relay_set(RELAY2_GPIO, relay2_on);
        }
    } else if (relay2_interval_counter < relay2_interval.interval_ms) {
        /* Wyłącz relay */
        if (relay2_on) {
            relay2_on = false;
            relay_set(RELAY2_GPIO, relay2_on);
        }
    } else {
        /* Resetuj licznik na koniec cyklu */
        relay2_interval_counter = 0;
        ESP_LOGI(TAG, "Relay 2 interval cycle complete");
    }
}

/* ================== FUNKCJE TRYBU INTERWAŁOWEGO ================== */

void relay_set_relay1_interval(uint32_t interval_ms, uint32_t on_time_ms)
{
    if (on_time_ms > interval_ms) {
        ESP_LOGW(TAG, "Relay 1: on_time_ms (%lu) nie może być większy niż interval_ms (%lu)",
                 on_time_ms, interval_ms);
        return;
    }

    relay1_interval.interval_ms = interval_ms;
    relay1_interval.on_time_ms = on_time_ms;
    relay1_interval_counter = 0;

    ESP_LOGI(TAG, "Relay 1 interval set: interval=%lu ms, on_time=%lu ms",
             interval_ms, on_time_ms);
}

void relay_set_relay2_interval(uint32_t interval_ms, uint32_t on_time_ms)
{
    if (on_time_ms > interval_ms) {
        ESP_LOGW(TAG, "Relay 2: on_time_ms (%lu) nie może być większy niż interval_ms (%lu)",
                 on_time_ms, interval_ms);
        return;
    }

    relay2_interval.interval_ms = interval_ms;
    relay2_interval.on_time_ms = on_time_ms;
    relay2_interval_counter = 0;

    ESP_LOGI(TAG, "Relay 2 interval set: interval=%lu ms, on_time=%lu ms",
             interval_ms, on_time_ms);
}

void relay_set_relay1_interval_enabled(bool enabled)
{
    relay1_interval.enabled = enabled;

    if (enabled) {
        relay1_interval_counter = 0;
        if (relay1_interval_timer == NULL) {
            relay1_interval_timer = xTimerCreate(
                "relay1_interval",
                pdMS_TO_TICKS(100),
                pdTRUE,
                NULL,
                relay1_interval_timer_callback
            );
            if (relay1_interval_timer == NULL) {
                ESP_LOGE(TAG, "Failed to create relay1_interval_timer!");
                relay1_interval.enabled = false;
                return;
            }
        }
        xTimerStart(relay1_interval_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Relay 1 interval mode ENABLED");
    } else {
        if (relay1_interval_timer != NULL) {
            xTimerStop(relay1_interval_timer, portMAX_DELAY);
        }
        relay1_on = false;
        relay_set(RELAY1_GPIO, relay1_on);
        ESP_LOGI(TAG, "Relay 1 interval mode DISABLED");
    }
}

void relay_set_relay2_interval_enabled(bool enabled)
{
    relay2_interval.enabled = enabled;

    if (enabled) {
        relay2_interval_counter = 0;
        if (relay2_interval_timer == NULL) {
            relay2_interval_timer = xTimerCreate(
                "relay2_interval",
                pdMS_TO_TICKS(100),
                pdTRUE,
                NULL,
                relay2_interval_timer_callback
            );
            if (relay2_interval_timer == NULL) {
                ESP_LOGE(TAG, "Failed to create relay2_interval_timer!");
                relay2_interval.enabled = false;
                return;
            }
        }
        xTimerStart(relay2_interval_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Relay 2 interval mode ENABLED");
    } else {
        if (relay2_interval_timer != NULL) {
            xTimerStop(relay2_interval_timer, portMAX_DELAY);
        }
        relay2_on = false;
        relay_set(RELAY2_GPIO, relay2_on);
        ESP_LOGI(TAG, "Relay 2 interval mode DISABLED");
    }
}

void relay_disable_all_intervals(void)
{
    relay_set_relay1_interval_enabled(false);
    relay_set_relay2_interval_enabled(false);
}

relay_interval_config_t relay_get_relay1_interval_config(void)
{
    return relay1_interval;
}

relay_interval_config_t relay_get_relay2_interval_config(void)
{
    return relay2_interval;
}
