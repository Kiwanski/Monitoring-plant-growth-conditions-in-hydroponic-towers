#ifndef LEVEL_SENSOR_H
#define LEVEL_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

// ============== KONFIGURACJA ==============
#define LEVEL_SENSOR_PIN GPIO_NUM_34     // Pin GPIO podłączony do czujnika
#define DEBOUNCE_TIME_MS 500             // Czas histerezowy (ms)
#define SENSOR_POLL_INTERVAL_MS 50       // Czas między odczytami (ms)

// ============== INICJALIZACJA ==============
/**
 * Inicjalizuje czujnik poziomów (GPIO, pull-up, itp)
 * Należy wywołać raz w app_main()
 */
void level_sensor_init(void);

// ============== FUNKCJE OBSŁUGI ==============
/**
 * Uruchamia zadanie FreeRTOS do odczytywania czujnika
 * Powinno być wywołane po level_sensor_init()
 */
void level_sensor_start_task(void);

// ============== FUNKCJE ZWRACAJĄCE STAN ==============
/**
 * Pobierz surowy stan pinu (bez debouncing)
 * @return 0 = pin LOW (brak wody), 1 = pin HIGH (jest woda)
 */
uint8_t level_sensor_get_raw(void);

/**
 * Pobierz stan czujnika po deboucingu
 * @return 0 = brak wody, 1 = jest woda
 */
uint8_t level_sensor_get_debounced(void);

/**
 * Sprawdź czy jest woda w zbiorniku
 * @return true = woda jest (pin HIGH), false = brak wody (pin LOW)
 */
bool level_sensor_has_water(void);

/**
 * Sprawdź czy zbiornik jest pusty
 * @return true = pusty (pin LOW), false = nie pusty
 */
bool level_sensor_is_empty(void);

#endif // LEVEL_SENSOR_H
