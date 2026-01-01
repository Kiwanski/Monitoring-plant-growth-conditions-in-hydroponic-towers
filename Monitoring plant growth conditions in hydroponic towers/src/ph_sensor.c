#include "ph_sensor.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "PH_SENSOR";

// ================== POMOCNICZE ==================

static void str_to_upper(char *str) {
    while (*str) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}

// ================== NVS ==================

esp_err_t ph_sensor_save_to_nvs(ph_sensor_t *sensor) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, NVS_KEY_NEUTRAL, &sensor->neutral_voltage, sizeof(sensor->neutral_voltage));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_ACID, &sensor->acid_voltage, sizeof(sensor->acid_voltage));
    }
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    return err;
}

static esp_err_t load_from_nvs(ph_sensor_t *sensor) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t size = sizeof(float);
    err = nvs_get_blob(handle, NVS_KEY_NEUTRAL, &sensor->neutral_voltage, &size);
    if (err != ESP_OK) sensor->neutral_voltage = PH_DEFAULT_NEUTRAL_VOLTAGE;

    size = sizeof(float);
    err = nvs_get_blob(handle, NVS_KEY_ACID, &sensor->acid_voltage, &size);
    if (err != ESP_OK) sensor->acid_voltage = PH_DEFAULT_ACID_VOLTAGE;

    nvs_close(handle);
    return ESP_OK;
}

// ================== API ==================

esp_err_t ph_sensor_init(ph_sensor_t *sensor) {
    if (!sensor) return ESP_ERR_INVALID_ARG;

    // Domyślne wartości
    sensor->neutral_voltage = PH_DEFAULT_NEUTRAL_VOLTAGE;
    sensor->acid_voltage    = PH_DEFAULT_ACID_VOLTAGE;
    sensor->temperature     = 25.0f;

    // Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed");
        return ret;
    }

    load_from_nvs(sensor);

    // Konfiguracja ADC1 (12 bit, attenuation 11dB ~0-3.6V)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                             ADC_WIDTH_BIT_12, 1100, &sensor->adc_chars);

    ESP_LOGI(TAG, "PH sensor initialized. Neutral=%.2f mV, Acid=%.2f mV",
             sensor->neutral_voltage, sensor->acid_voltage);

    return ESP_OK;
}

esp_err_t ph_sensor_read_adc(ph_sensor_t *sensor, adc1_channel_t channel, float temperature, float *ph) {
    if (!sensor || !ph) return ESP_ERR_INVALID_ARG;

    int raw = adc1_get_raw(channel);
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(raw, &sensor->adc_chars);

    sensor->last_voltage = (float)voltage_mv;
    sensor->temperature = temperature;
    *ph = ph_sensor_calculate(sensor, sensor->last_voltage, temperature);

    ESP_LOGI(TAG, "ADC raw=%d, voltage=%dmV, pH=%.2f", raw, voltage_mv, *ph);

    return ESP_OK;
}

float ph_sensor_calculate(ph_sensor_t *sensor, float voltage_mv, float temperature) {
    (void)temperature; // na razie nie używamy kompensacji temperaturowej

    // uproszczony wzór: pH = 7 + 3 * (V - neutral) / (neutral - acid)
    float slope = 3.0f / (sensor->neutral_voltage - sensor->acid_voltage);
    float ph = 7.0f + slope * (voltage_mv - sensor->neutral_voltage);
    sensor->ph_value = ph;
    return ph;
}

esp_err_t ph_sensor_calibration(ph_sensor_t *sensor, const char *cmd, float voltage_mv) {
    if (!sensor || !cmd) return ESP_ERR_INVALID_ARG;

    char buf[16];
    strncpy(buf, cmd, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    str_to_upper(buf);

    if (strcmp(buf, "ENTERPH") == 0) {
        ESP_LOGI(TAG, ">>> Enter PH Calibration Mode <<<");
        return ESP_OK;
    } else if (strcmp(buf, "CALPH7") == 0) {
        sensor->neutral_voltage = voltage_mv;
        ESP_LOGI(TAG, ">>> Calibrated Neutral pH=7.0 at %.2f mV <<<", voltage_mv);
        return ESP_OK;
    } else if (strcmp(buf, "CALPH4") == 0) {
        sensor->acid_voltage = voltage_mv;
        ESP_LOGI(TAG, ">>> Calibrated Acid pH=4.0 at %.2f mV <<<", voltage_mv);
        return ESP_OK;
    } else if (strcmp(buf, "EXITPH") == 0) {
        ESP_LOGI(TAG, ">>> Saving calibration and exiting <<<");
        return ph_sensor_save_to_nvs(sensor);
    } else {
        ESP_LOGW(TAG, ">>> Unknown calibration command: %s <<<", buf);
        return ESP_ERR_INVALID_ARG;
    }
}
