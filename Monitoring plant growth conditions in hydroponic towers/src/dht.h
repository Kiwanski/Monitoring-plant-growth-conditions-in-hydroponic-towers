#ifndef DHT_H
#define DHT_H

#include "esp_err.h"
#include "driver/gpio.h"

#define DHT_GPIO GPIO_NUM_4   // pin, do którego podłączamy czujnik

esp_err_t dht22_read(float *temperature, float *humidity);

#endif // DHT_H
