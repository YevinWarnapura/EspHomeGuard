#include "ultrasonic.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"   


static const char* TAG = "ULTRA";

// UPDATE THESE TO MATCH YOUR WIRING
#define TRIG_PIN  GPIO_NUM_5
#define ECHO_PIN  GPIO_NUM_18

void ultrasonic_init()
{
    ESP_LOGI(TAG, "Initializing ultrasonic...");

    gpio_config_t trig = {};
    trig.mode = GPIO_MODE_OUTPUT;
    trig.pin_bit_mask = 1ULL << TRIG_PIN;
    gpio_config(&trig);

    gpio_config_t echo = {};
    echo.mode = GPIO_MODE_INPUT;
    echo.pin_bit_mask = 1ULL << ECHO_PIN;
    gpio_config(&echo);

    gpio_set_level(TRIG_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Ultrasonic ready");
}

int ultrasonic_get_distance_cm()
{
    // Make sure trigger starts LOW
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);   // <-- CHANGED

    // 10us trigger pulse
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);  // <-- CHANGED
    gpio_set_level(TRIG_PIN, 0);

    // WAIT FOR ECHO HIGH
    int timeout = 30000;
    while (gpio_get_level(ECHO_PIN) == 0) {
        if (--timeout == 0) return -1;
        esp_rom_delay_us(1);   // <-- CHANGED
    }

    // MEASURE HIGH TIME
    int64_t start = esp_timer_get_time();
    timeout = 30000;
    while (gpio_get_level(ECHO_PIN) == 1) {
        if (--timeout == 0) return -1;
        esp_rom_delay_us(1);   // <-- CHANGED
    }
    int64_t end = esp_timer_get_time();

    int duration_us = (int)(end - start);

    // Convert to cm
    int distance_cm = duration_us / 58;

    if (distance_cm < 2 || distance_cm > 400)
        return -1;

    return distance_cm;
}
