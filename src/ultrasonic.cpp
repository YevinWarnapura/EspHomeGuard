#include "ultrasonic.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

static const char* TAG = "ULTRA";

#define TRIG_PIN  GPIO_NUM_5
#define ECHO_PIN  GPIO_NUM_18

#define US_TIMEOUT_US 30000
#define NUM_SAMPLES   5           // number of readings to average
#define DEBOUNCE_DIFF_CM 10       // max allowed jump between readings

static int last_valid_distance = -1;

void ultrasonic_init()
{
    ESP_LOGI(TAG, "Initializing ultrasonic...");

    gpio_config_t trig = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << TRIG_PIN
    };
    gpio_config(&trig);

    gpio_config_t echo = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << ECHO_PIN
    };
    gpio_config(&echo);

    gpio_set_level(TRIG_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Ultrasonic ready");
}

static int measure_distance_once()
{
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);

    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    int timeout = US_TIMEOUT_US;
    while (gpio_get_level(ECHO_PIN) == 0) {
        if (--timeout <= 0) return -1;
        esp_rom_delay_us(1);
    }

    int64_t start = esp_timer_get_time();

    timeout = US_TIMEOUT_US;
    while (gpio_get_level(ECHO_PIN) == 1) {
        if (--timeout <= 0) return -1;
        esp_rom_delay_us(1);
    }

    int64_t end = esp_timer_get_time();
    int duration_us = (int)(end - start);

    int distance_cm = duration_us / 58;

    if (distance_cm < 2 || distance_cm > 400)
        return -1;

    return distance_cm;
}

int ultrasonic_get_distance_cm()
{
    int sample_sum = 0;
    int valid_count = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        int d = measure_distance_once();
        if (d > 0) {
            sample_sum += d;
            valid_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // small delay between samples
    }

    if (valid_count == 0) {
        ESP_LOGW(TAG, "No valid ultrasonic samples");
        return -1;
    }

    int avg = sample_sum / valid_count;

    // Debounce: ignore sudden unrealistic jumps
    if (last_valid_distance != -1 &&
        abs(avg - last_valid_distance) > DEBOUNCE_DIFF_CM)
    {
        ESP_LOGW(TAG, "Debounce triggered: old=%d new=%d", last_valid_distance, avg);
        return last_valid_distance; // keep previous stable reading
    }

    last_valid_distance = avg;
    return avg;
}
