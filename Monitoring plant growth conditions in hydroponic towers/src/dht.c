#include "dht.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

static const char *TAG = "DHT22";

// Prosta funkcja opóźnienia w mikrosekundach
static void delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

esp_err_t dht22_read(float *temperature, float *humidity)
{
    int data[40] = {0};
    uint8_t bytes[5] = {0};

    // Inicjalizacja - sygnał startu
    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO, 0);
    delay_us(1000 * 20); // 20 ms
    gpio_set_level(DHT_GPIO, 1);
    delay_us(40);

    gpio_set_direction(DHT_GPIO, GPIO_MODE_INPUT);

    // Synchronizacja
    int timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 1 && timeout++ < 100) delay_us(1);
    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 0 && timeout++ < 100) delay_us(1);
    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 1 && timeout++ < 100) delay_us(1);

    // Odczyt 40 bitów
    for (int i = 0; i < 40; i++) {
        timeout = 0;
        while (gpio_get_level(DHT_GPIO) == 0 && timeout++ < 100) delay_us(1);

        int t = 0;
        while (gpio_get_level(DHT_GPIO) == 1 && t < 100) {
            delay_us(1);
            t++;
        }
        if (t > 40) data[i] = 1;
        else data[i] = 0;
    }

    // Składanie bajtów
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 8; j++) {
            bytes[i] <<= 1;
            bytes[i] |= data[i * 8 + j];
        }
    }

    // Sprawdzenie sumy kontrolnej
    if (((bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF) != bytes[4]) {
        ESP_LOGE(TAG, "Checksum error!");
        return ESP_FAIL;
    }

    // Konwersja na wartości fizyczne
    int raw_humidity = (bytes[0] << 8) | bytes[1];
    int raw_temperature = (bytes[2] << 8) | bytes[3];

    if (raw_temperature & 0x8000) { // sprawdzenie znaku
        raw_temperature = -(raw_temperature & 0x7FFF);
    }

    *humidity = raw_humidity / 10.0;
    *temperature = raw_temperature / 10.0;

    return ESP_OK;
}
