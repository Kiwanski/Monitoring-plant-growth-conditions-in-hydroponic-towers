// File: ds18b20.h
#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include "onewire.h" // potrzebne dla deklaracji OneWire

// Request temperature conversion on all devices
bool ds18_request_temperatures(OneWire *ow);
// Read temperature (C) from device index (0 = first). If not present returns NAN
float ds18_get_temp_c_by_index(OneWire *ow, int index);

#endif // DS18B20_H
