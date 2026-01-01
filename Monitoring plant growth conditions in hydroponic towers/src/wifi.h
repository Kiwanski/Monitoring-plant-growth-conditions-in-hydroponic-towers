#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h> 

// Inicjalizacja Wi-Fi w trybie stacji (ESP32 jako klient)
esp_err_t wifi_init_sta(const char *ssid, const char *password);

// Inicjalizacja Wi-Fi w trybie Access Point (ESP32 jako AP)
esp_err_t wifi_init_softap(void);

// Sprawdzenie, czy ESP32 jest połączone z siecią Wi-Fi
bool wifi_is_connected(void);

#endif // WIFI_H
