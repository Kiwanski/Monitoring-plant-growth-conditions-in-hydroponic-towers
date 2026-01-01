#include "ds1302.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "DS1302";

// ================= BCD konwersja =================
static uint8_t bcd2dec(uint8_t val) { return ((val >> 4) * 10) + (val & 0x0F); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

// ================= Low-level =================
static void ds1302_start(void) {
    gpio_set_level(DS1302_RST_PIN, 1);
    ets_delay_us(4);
}

static void ds1302_stop(void) {
    gpio_set_level(DS1302_RST_PIN, 0);
    ets_delay_us(4);
}

static void ds1302_write_byte(uint8_t data) {
    gpio_set_direction(DS1302_DAT_PIN, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(DS1302_DAT_PIN, (data >> i) & 1);
        ets_delay_us(2);
        gpio_set_level(DS1302_CLK_PIN, 1);
        ets_delay_us(2);
        gpio_set_level(DS1302_CLK_PIN, 0);
    }
}

static uint8_t ds1302_read_byte(void) {
    uint8_t data = 0;
    gpio_set_direction(DS1302_DAT_PIN, GPIO_MODE_INPUT);
    for (int i = 0; i < 8; i++) {
        int bit = gpio_get_level(DS1302_DAT_PIN);
        data |= (bit << i);
        ets_delay_us(2);
        gpio_set_level(DS1302_CLK_PIN, 1);
        ets_delay_us(2);
        gpio_set_level(DS1302_CLK_PIN, 0);
    }
    return data;
}

// ================= Rejestry =================
void ds1302_write_register(uint8_t reg, uint8_t value) {
    ds1302_start();
    ds1302_write_byte((reg << 1) | 0x80); // zapis
    ds1302_write_byte(value);
    ds1302_stop();
}

uint8_t ds1302_read_register(uint8_t reg) {
    uint8_t value;
    ds1302_start();
    ds1302_write_byte((reg << 1) | 0x81); // odczyt
    value = ds1302_read_byte();
    ds1302_stop();
    return value;
}

// ================= Public API =================
void ds1302_init(void) {
    gpio_reset_pin(DS1302_CLK_PIN);
    gpio_reset_pin(DS1302_DAT_PIN);
    gpio_reset_pin(DS1302_RST_PIN);

    gpio_set_direction(DS1302_CLK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DS1302_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DS1302_DAT_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(DS1302_CLK_PIN, 0);
    gpio_set_level(DS1302_RST_PIN, 0);
    gpio_set_level(DS1302_DAT_PIN, 0);

    // odblokuj oscylator i wyłącz write protect
    uint8_t sec = ds1302_read_register(0x00);
    if (sec & 0x80) ds1302_write_register(0x00, sec & 0x7F);

    uint8_t ctrl = ds1302_read_register(0x07);
    if (ctrl & 0x80) ds1302_write_register(0x07, ctrl & 0x7F);

    ESP_LOGI(TAG, "DS1302 initialized");
}

void ds1302_set_time(const ds1302_time_t *t) {
    ds1302_write_register(0x00, dec2bcd(t->sec) & 0x7F);
    ds1302_write_register(0x01, dec2bcd(t->min));
    ds1302_write_register(0x02, dec2bcd(t->hour));
    ds1302_write_register(0x03, dec2bcd(t->day));
    ds1302_write_register(0x04, dec2bcd(t->month));
    ds1302_write_register(0x05, dec2bcd(t->dow));
    ds1302_write_register(0x06, dec2bcd(t->year % 100)); // tylko 2 cyfry
}

void ds1302_get_time(ds1302_time_t *t) {
    t->sec   = bcd2dec(ds1302_read_register(0x00) & 0x7F);
    t->min   = bcd2dec(ds1302_read_register(0x01));
    t->hour  = bcd2dec(ds1302_read_register(0x02));
    t->day   = bcd2dec(ds1302_read_register(0x03));
    t->month = bcd2dec(ds1302_read_register(0x04));
    t->dow   = bcd2dec(ds1302_read_register(0x05));
    t->year  = 2000 + bcd2dec(ds1302_read_register(0x06));
}

// ================= Obliczanie dnia tygodnia =================
uint8_t calculate_dow(uint16_t year, uint8_t month, uint8_t day) {
    if (month < 3) { month += 12; year--; }
    uint16_t K = year % 100;
    uint16_t J = year / 100;
    uint8_t h = (day + 13*(month+1)/5 + K + K/4 + J/4 + 5*J) % 7;
    // konwersja na 1=poniedziałek ... 7=niedziela
    uint8_t d = ((h + 5) % 7) + 1;
    return d;
}

// ================= Ustawienie czasu kompilacji =================
void ds1302_set_compile_time(void) {
    ds1302_time_t t = {0};
    const char* date = __DATE__;
    const char* time = __TIME__;

    // miesiąc
    if (strncmp(date, "Jan", 3) == 0) t.month = 1;
    else if (strncmp(date, "Feb", 3) == 0) t.month = 2;
    else if (strncmp(date, "Mar", 3) == 0) t.month = 3;
    else if (strncmp(date, "Apr", 3) == 0) t.month = 4;
    else if (strncmp(date, "May", 3) == 0) t.month = 5;
    else if (strncmp(date, "Jun", 3) == 0) t.month = 6;
    else if (strncmp(date, "Jul", 3) == 0) t.month = 7;
    else if (strncmp(date, "Aug", 3) == 0) t.month = 8;
    else if (strncmp(date, "Sep", 3) == 0) t.month = 9;
    else if (strncmp(date, "Oct", 3) == 0) t.month = 10;
    else if (strncmp(date, "Nov", 3) == 0) t.month = 11;
    else if (strncmp(date, "Dec", 3) == 0) t.month = 12;

    char buf[5];
    strncpy(buf, date+4, 2); buf[2]=0; t.day = atoi(buf);
    strncpy(buf, date+7, 4); buf[4]=0; t.year = atoi(buf);

    strncpy(buf, time, 2); buf[2]=0; t.hour = atoi(buf);
    strncpy(buf, time+3, 2); buf[2]=0; t.min = atoi(buf);
    strncpy(buf, time+6, 2); buf[2]=0; t.sec = atoi(buf);

    t.dow = calculate_dow(t.year, t.month, t.day);

    ds1302_set_time(&t);
    ESP_LOGI(TAG, "RTC set to compile time: %02d:%02d:%02d %02d/%02d/%04d (DOW=%d)",
             t.hour, t.min, t.sec, t.day, t.month, t.year, t.dow);
}
