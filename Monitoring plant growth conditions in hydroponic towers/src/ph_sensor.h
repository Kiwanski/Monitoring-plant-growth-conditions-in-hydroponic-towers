#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include "esp_err.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// Domyślne wartości kalibracyjne (mV)
#define PH_DEFAULT_NEUTRAL_VOLTAGE 1500.0f   // odpowiada pH 7
#define PH_DEFAULT_ACID_VOLTAGE    2032.44f  // odpowiada pH 4

// Klucze NVS
#define NVS_NAMESPACE   "ph_sensor"
#define NVS_KEY_NEUTRAL "neutral_v"
#define NVS_KEY_ACID    "acid_v"

typedef struct {
    float ph_value;
    float acid_voltage;
    float neutral_voltage;
    float last_voltage;
    float temperature;
    esp_adc_cal_characteristics_t adc_chars;
} ph_sensor_t;

// Inicjalizacja czujnika pH (ładuje wartości kalibracji z NVS)
esp_err_t ph_sensor_init(ph_sensor_t *sensor);

// Odczyt z ADC1 wybranego kanału i przeliczenie na pH
esp_err_t ph_sensor_read_adc(ph_sensor_t *sensor, adc1_channel_t channel, float temperature, float *ph);

// Przeliczenie napięcia (mV) na pH
float ph_sensor_calculate(ph_sensor_t *sensor, float voltage_mv, float temperature);

// Tryb kalibracji (komenda: "ENTERPH", "CALPH4", "CALPH7", "EXITPH")
esp_err_t ph_sensor_calibration(ph_sensor_t *sensor, const char *cmd, float voltage_mv);

// Zapis wartości kalibracji do NVS
esp_err_t ph_sensor_save_to_nvs(ph_sensor_t *sensor);

#endif // PH_SENSOR_H
