#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_rom_sys.h" // esp_rom_delay_us

typedef struct {
    uint8_t pin;
} OneWire;

// Inicjalizacja magistrali 1-Wire
void onewire_init(OneWire *ow, uint8_t pin);

// Reset magistrali, zwraca 1 jeśli obecny jest urządzenie
uint8_t onewire_reset(OneWire *ow);

// Odczyt / zapis bitu
void onewire_write_bit(OneWire *ow, uint8_t bit);
uint8_t onewire_read_bit(OneWire *ow);

// Odczyt / zapis bajtu
void onewire_write_byte(OneWire *ow, uint8_t byte);
uint8_t onewire_read_byte(OneWire *ow);

// Skip ROM / Select ROM
void onewire_skip_rom(OneWire *ow);
void onewire_select_rom(OneWire *ow, const uint8_t rom[8]);

#endif // ONEWIRE_H
