#pragma once
#include "driver/gpio.h"
#include <stdint.h>

// Piny DS1302 – można zmienić
#define DS1302_CLK_PIN   GPIO_NUM_12
#define DS1302_DAT_PIN   GPIO_NUM_14
#define DS1302_RST_PIN   GPIO_NUM_27

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t dow; // 1 = poniedziałek
} ds1302_time_t;

// Funkcje
void ds1302_init(void);
void ds1302_set_time(const ds1302_time_t *t);
void ds1302_get_time(ds1302_time_t *t);
void ds1302_set_compile_time(void);

// Rejestry
void ds1302_write_register(uint8_t reg, uint8_t value);
uint8_t ds1302_read_register(uint8_t reg);

// Obliczanie dnia tygodnia
uint8_t calculate_dow(uint16_t year, uint8_t month, uint8_t day);
