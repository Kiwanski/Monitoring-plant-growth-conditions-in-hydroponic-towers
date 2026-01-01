#ifndef MQTT_H
#define MQTT_H

#include "esp_err.h"
#include <stdbool.h>

/* Deklaracje funkcji używanych w main.c */
esp_err_t mqtt_init(const char *broker_url);
bool mqtt_publish(const char *topic, const char *data);

#endif // MQTT_H
