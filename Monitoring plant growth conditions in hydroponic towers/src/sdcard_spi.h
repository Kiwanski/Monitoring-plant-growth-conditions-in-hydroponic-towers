#ifndef SDCARD_SPI_H
#define SDCARD_SPI_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Inicjalizacja karty SD i montowanie w SPI
 * @return ESP_OK jeśli OK, ESP_FAIL lub inny kod błędu w przeciwnym wypadku
 */
esp_err_t sensor_sdcard_init(void);

/**
 * Odmontowanie karty SD
 */
esp_err_t sensor_sdcard_unmount(void);

/**
 * Dopisuje dane czujnika do pliku JSON.
 * Jeśli timestamp==NULL, funkcja dopisuje aktualny czas w ms od startu ESP32.
 *
 * @param path Ścieżka do pliku JSON (np. "/sdcard/sensordata.json")
 * @param sensor_id Identyfikator czujnika (np. "temp", "hum")
 * @param value Wartość zmierzonego parametru
 * @param timestamp NULL lub string z timestampem
 * @return ESP_OK jeśli zapis się powiódł
 */
esp_err_t sensor_json_append(const char *path, const char *sensor_id, double value, const char *timestamp);

/**
 * Usuwa wszystkie wpisy w pliku JSON, gdzie podane pole ma podaną wartość.
 * Przykład: usunięcie wszystkich wpisów typu "temp"
 *
 * @param path Ścieżka do pliku JSON
 * @param field Nazwa pola (np. "sensor_id")
 * @param value Wartość pola (np. "temp")
 * @return ESP_OK jeśli operacja się powiodła
 */
esp_err_t sensor_json_remove_by_field(const char *path, const char *field, const char *value);

/**
 * Odczytuje i loguje zawartość pliku JSON
 * @param path Ścieżka do pliku JSON
 * @return ESP_OK jeśli odczyt się powiódł
 */
esp_err_t sensor_json_read(const char *path);

/**
 * Append a single JSON object (line) to a file as NDJSON (newline-delimited JSON).
 * This function opens the file in append mode and writes the provided JSON string
 * followed by a newline. It flushes and syncs to reduce data loss on power loss.
 */
esp_err_t sensor_ndjson_append(const char *path, const char *json_line);

/**
 * Returns true if SD card is currently mounted.
 */
bool sensor_sdcard_is_mounted(void);
#endif // SDCARD_SPI_H
