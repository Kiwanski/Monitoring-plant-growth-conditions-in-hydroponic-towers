#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include "driver/gpio.h"

/* ================== KONFIGURACJA PINÓW ================== */

/* Piny sterujące przekaźnikami */
#define RELAY1_GPIO   GPIO_NUM_26
#define RELAY2_GPIO   GPIO_NUM_25

/* Piny przycisków (do GND; wewnętrzne pull-up włączone) */
#define BUTTON1_GPIO  GPIO_NUM_32
#define BUTTON2_GPIO  GPIO_NUM_33

/* Większość gotowych modułów przekaźników to active-LOW (0 = ON) */
#define RELAY_ACTIVE_LOW  1   /* ustaw 0 jeśli Twój moduł jest active-HIGH */

/* ================== FUNKCJE PUBLICZNE ================== */

/**
 * Inicjalizuje przekaźniki
 */
void relay_init(void);

/**
 * Inicjalizuje przyciski z przerwaniami
 */
void relay_buttons_init(void);

/**
 * Wł/wył przekaźnik 1
 */
void relay_toggle_relay1(void);

/**
 * Wł/wył przekaźnik 2
 */
void relay_toggle_relay2(void);

/**
 * Włącz przekaźnik 1
 */
void relay_set_relay1_on(void);

/**
 * Wyłącz przekaźnik 1
 */
void relay_set_relay1_off(void);

/**
 * Włącz przekaźnik 2
 */
void relay_set_relay2_on(void);

/**
 * Wyłącz przekaźnik 2
 */
void relay_set_relay2_off(void);

/**
 * Zwraca stan przekaźnika 1
 */
bool relay_get_relay1_state(void);

/**
 * Zwraca stan przekaźnika 2
 */
bool relay_get_relay2_state(void);

/**
 * Zwraca kolejkę dla zdarzeń GPIO
 */
void* relay_get_event_queue(void);

/* ================== TRYB INTERWAŁOWY ================== */

/**
 * Struktura dla konfiguracji trybu interwałowego
 * interval_ms: okres włączenia/wyłączenia (ms)
 * on_time_ms: czas włączenia (ms)
 * enabled: czy tryb interwałowy jest aktywny
 */
typedef struct {
    uint32_t interval_ms;
    uint32_t on_time_ms;
    bool enabled;
} relay_interval_config_t;

/**
 * Ustawia parametry trybu interwałowego dla relay 1
 * interval_ms: okres pełnego cyklu
 * on_time_ms: ile czasu relay ma być włączony w cyklu
 */
void relay_set_relay1_interval(uint32_t interval_ms, uint32_t on_time_ms);

/**
 * Ustawia parametry trybu interwałowego dla relay 2
 * interval_ms: okres pełnego cyklu
 * on_time_ms: ile czasu relay ma być włączony w cyklu
 */
void relay_set_relay2_interval(uint32_t interval_ms, uint32_t on_time_ms);

/**
 * Włącza/wyłącza tryb interwałowy dla relay 1
 */
void relay_set_relay1_interval_enabled(bool enabled);

/**
 * Włącza/wyłącza tryb interwałowy dla relay 2
 */
void relay_set_relay2_interval_enabled(bool enabled);

/**
 * Wyłącza tryb interwałowy dla obu relay
 */
void relay_disable_all_intervals(void);

/**
 * Zwraca konfigurację trybu interwałowego dla relay 1
 */
relay_interval_config_t relay_get_relay1_interval_config(void);

/**
 * Zwraca konfigurację trybu interwałowego dla relay 2
 */
relay_interval_config_t relay_get_relay2_interval_config(void);

#endif /* RELAY_H */
