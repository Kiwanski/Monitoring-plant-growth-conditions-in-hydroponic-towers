#include "onewire.h"
#include "driver/gpio.h"

// Inicjalizacja magistrali
void onewire_init(OneWire *ow, uint8_t pin)
{
    ow->pin = pin;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
}

// Reset 1-Wire
uint8_t onewire_reset(OneWire *ow)
{
    uint8_t presence;

    // Pull low na 480 us
    gpio_set_direction(ow->pin, GPIO_MODE_OUTPUT);
    gpio_set_level(ow->pin, 0);
    esp_rom_delay_us(480);

    // Zwalniamy magistralę
    gpio_set_direction(ow->pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(70);

    presence = gpio_get_level(ow->pin) == 0 ? 1 : 0;

    esp_rom_delay_us(410);
    return presence;
}

// Zapisywanie bitu
void onewire_write_bit(OneWire *ow, uint8_t bit)
{
    if (bit) {
        gpio_set_direction(ow->pin, GPIO_MODE_OUTPUT);
        gpio_set_level(ow->pin, 0);
        esp_rom_delay_us(10);
        gpio_set_level(ow->pin, 1);
        gpio_set_direction(ow->pin, GPIO_MODE_INPUT);
        esp_rom_delay_us(55);
    } else {
        gpio_set_direction(ow->pin, GPIO_MODE_OUTPUT);
        gpio_set_level(ow->pin, 0);
        esp_rom_delay_us(65);
        gpio_set_level(ow->pin, 1);
        gpio_set_direction(ow->pin, GPIO_MODE_INPUT);
        esp_rom_delay_us(5);
    }
}

// Odczyt bitu
uint8_t onewire_read_bit(OneWire *ow)
{
    uint8_t bit;

    gpio_set_direction(ow->pin, GPIO_MODE_OUTPUT);
    gpio_set_level(ow->pin, 0);
    esp_rom_delay_us(3);
    gpio_set_direction(ow->pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(10);
    bit = gpio_get_level(ow->pin);
    esp_rom_delay_us(53);

    return bit;
}

// Zapisywanie bajtu
void onewire_write_byte(OneWire *ow, uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        onewire_write_bit(ow, (byte >> i) & 0x01);
    }
}

// Odczyt bajtu
uint8_t onewire_read_byte(OneWire *ow)
{
    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (onewire_read_bit(ow)) {
            byte |= (1 << i);
        }
    }
    return byte;
}

// Skip ROM
void onewire_skip_rom(OneWire *ow)
{
    onewire_write_byte(ow, 0xCC);
}

// Select ROM
void onewire_select_rom(OneWire *ow, const uint8_t rom[8])
{
    onewire_write_byte(ow, 0x55);
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(ow, rom[i]);
    }
}
