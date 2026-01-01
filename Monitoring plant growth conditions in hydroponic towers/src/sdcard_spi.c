#include "sdcard_spi.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include <inttypes.h> // for PRId64
#include "cJSON.h"    
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSOR_SD";

static sdmmc_card_t *card;
static const char mount_point[] = "/sdcard";
static sdmmc_host_t host = SDSPI_HOST_DEFAULT();

// konfiguracja montowania
static esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 10,
    .allocation_unit_size = 0  // Use default allocation unit size
};

/********************
 * Inicjalizacja SD
 ********************/
esp_err_t sensor_sdcard_init(void) {
    ESP_LOGI(TAG, "Inicjalizacja karty SD (SPI)...");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 23,
        .miso_io_num = 19,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 5;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Montuję SD na %s...", mount_point);
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Nie udało się zamontować SD: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    ESP_LOGI(TAG, "Karta SD zamontowana pomyślnie!");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

/********************
 * Odmontowanie SD
 ********************/
esp_err_t sensor_sdcard_unmount(void) {
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Karta SD odmontowana.");
    }
    spi_bus_free(host.slot);
    return ret;
}

/********************
 * Dopisywanie danych do JSON
 ********************/
esp_err_t sensor_json_append(const char *path, const char *sensor_id, double value, const char *timestamp) {
    FILE *f = fopen(path, "r+");
    cJSON *root = NULL;

    if (f) {
        // Odczytaj istniejący plik
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *data = malloc(size + 1);
        if (!data) { fclose(f); return ESP_FAIL; }
        fread(data, 1, size, f);
        data[size] = 0;

        root = cJSON_Parse(data);
        free(data);
        fclose(f);

        if (!root) {
            ESP_LOGW(TAG, "Błąd parsowania JSON, utworzę nową tablicę");
            root = cJSON_CreateArray();
        }
    } else {
        // Plik nie istnieje – utwórz nowy
        root = cJSON_CreateArray();
    }

    // Dodaj nowy obiekt
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "sensor_id", sensor_id);
    cJSON_AddNumberToObject(entry, "value", value);

    if (timestamp) {
        cJSON_AddStringToObject(entry, "timestamp", timestamp);
    } else {
        int64_t t = esp_timer_get_time() / 1000;
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64, t);
        cJSON_AddStringToObject(entry, "timestamp", buf);
    }

    cJSON_AddItemToArray(root, entry);

    // Zapisz plik
    f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Nie mogę otworzyć %s do zapisu", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char *json_str = cJSON_Print(root);
    fprintf(f, "%s\n", json_str);
    fclose(f);
    cJSON_Delete(root);
    free(json_str);

    ESP_LOGI(TAG, "Zapisano JSON: %s", path);
    return ESP_OK;
}

/********************
 * Usuwanie wpisów po polu
 ********************/
esp_err_t sensor_json_remove_by_field(const char *path, const char *field, const char *value) {
    FILE *f = fopen(path, "r");
    if (!f) return ESP_FAIL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(size + 1);
    if (!data) { fclose(f); return ESP_FAIL; }
    fread(data, 1, size, f);
    data[size] = 0;
    fclose(f);

    cJSON *root = cJSON_Parse(data);
    free(data);
    if (!root) return ESP_FAIL;

    int array_size = cJSON_GetArraySize(root);
    for (int i = array_size - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *fobj = cJSON_GetObjectItem(item, field);
        if (fobj && strcmp(fobj->valuestring, value) == 0) {
            cJSON_DeleteItemFromArray(root, i);
        }
    }

    f = fopen(path, "w");
    if (!f) { cJSON_Delete(root); return ESP_FAIL; }

    char *json_str = cJSON_Print(root);
    fprintf(f, "%s\n", json_str);
    fclose(f);
    cJSON_Delete(root);
    free(json_str);

    ESP_LOGI(TAG, "Zaktualizowano JSON: %s", path);
    return ESP_OK;
}

/********************
 * Odczyt JSON
 ********************/
esp_err_t sensor_json_read(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return ESP_FAIL;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        ESP_LOGI(TAG, "Plik %s: %s", path, line);
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t sensor_ndjson_append(const char *path, const char *json_line) {
    if (!path || !json_line) return ESP_ERR_INVALID_ARG;
    
    // Spróbuj otworzyć w trybie append
    FILE *f = fopen(path, "a");
    if (!f) {
        // Jeśli plik nie istnieje, utwórz nowy
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE(TAG, "fopen(%s) failed: %s", path, strerror(errno));
            return ESP_FAIL;
        }
    }
    
    fprintf(f, "%s\n", json_line);
    fflush(f);
    fclose(f);
    
    return ESP_OK;
}

bool sensor_sdcard_is_mounted(void) {
    return card != NULL;
}
