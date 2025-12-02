#include "led.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// =========================
// Pin Definition
// =========================

// Change these to match your wiring
static const gpio_num_t LED_DISARMED = GPIO_NUM_15;   // green
static const gpio_num_t LED_ARMED    = GPIO_NUM_23;   // blue
static const gpio_num_t LED_ALARM    = GPIO_NUM_4;   // red

static const char* TAG = "LED";


// =========================
// Helper
// =========================
static void set_led(gpio_num_t pin, bool on)
{
    gpio_set_level(pin, on ? 1 : 0);
}


// =========================
// Initialization
// =========================
void led_init()
{
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;

    cfg.pin_bit_mask =
        (1ULL << LED_DISARMED) |
        (1ULL << LED_ARMED)    |
        (1ULL << LED_ALARM);

    gpio_config(&cfg);

    // startup state
    set_led(LED_DISARMED, 1);
    set_led(LED_ARMED,    0);
    set_led(LED_ALARM,    0);

    ESP_LOGI(TAG, "LED module initialized");
}


// =========================
// Basic State LEDs
// =========================
void led_set_disarmed()
{
    set_led(LED_DISARMED, 1);
    set_led(LED_ARMED,    0);
    set_led(LED_ALARM,    0);
}

void led_set_armed()
{
    set_led(LED_DISARMED, 0);
    set_led(LED_ARMED,    1);
    set_led(LED_ALARM,    0);
}

void led_set_alarm()
{
    set_led(LED_DISARMED, 0);
    set_led(LED_ARMED,    0);
    set_led(LED_ALARM,    1);
}


// =========================
// EXIT DELAY
// This fades the ARMED LED as time decreases
// sec_left goes from 15 → 0
// =========================
void led_set_exit_delay_level(int sec_left)
{
    // Clamp
    if (sec_left < 0) sec_left = 0;
    if (sec_left > 15) sec_left = 15;

    // Map time left to blinking frequency
    // More urgent → faster blink
    int period_ms;

    if (sec_left > 10) period_ms = 800;
    else if (sec_left > 5) period_ms = 400;
    else period_ms = 150;

    // Simple blink to show countdown visually
    static TickType_t last_toggle = 0;
    TickType_t now = xTaskGetTickCount();

    if (now - last_toggle >= pdMS_TO_TICKS(period_ms))
    {
        last_toggle = now;
        int current = gpio_get_level(LED_ARMED);
        set_led(LED_ARMED, !current);
    }
}
