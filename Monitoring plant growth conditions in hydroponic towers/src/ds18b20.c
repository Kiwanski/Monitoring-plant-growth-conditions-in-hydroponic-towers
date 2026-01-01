#include "onewire.h"
#include "ds18b20.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Dallas/Maxim CRC8
static uint8_t ds_crc8(const uint8_t *data, int len)
{
	uint8_t crc = 0;
	for (int i = 0; i < len; i++) {
		uint8_t inbyte = data[i];
		for (int j = 0; j < 8; j++) {
			uint8_t mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix) crc ^= 0x8C;
			inbyte >>= 1;
		}
	}
	return crc;
}

// Request temperature conversion on all devices (SKIP ROM)
bool ds18_request_temperatures(OneWire *ow)
{
	// Reset + presence
	if (!onewire_reset(ow)) {
		return false;
	}
	onewire_skip_rom(ow);      // broadcast
	onewire_write_byte(ow, 0x44); // CONVERT T
	return true;
}

// Read temperature (C) from device index
float ds18_get_temp_c_by_index(OneWire *ow, int index)
{
	(void)index; // w tej prostej implementacji obsługujemy tylko SKIP ROM / jedno urządzenie
	// Reset + presence
	if (!onewire_reset(ow)) {
		return NAN;
	}
	onewire_skip_rom(ow);
	onewire_write_byte(ow, 0xBE); // READ SCRATCHPAD

	uint8_t scratch[9];
	for (int i = 0; i < 9; i++) {
		scratch[i] = onewire_read_byte(ow);
	}

	// CRC check
	if (ds_crc8(scratch, 8) != scratch[8]) {
		return NAN;
	}

	int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
	// standardowa konwersja: resolution default 12-bit -> /16.0
	float temp_c = raw / 16.0f;
	return temp_c;
}

