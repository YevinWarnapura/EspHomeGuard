#pragma once
#include "driver/gpio.h"

// Call once in app_main()
void led_init();

// State indicators
void led_set_disarmed();
void led_set_armed();
void led_set_alarm();

// Exit delay indicator (0–15 seconds)
void led_set_exit_delay_level(int sec_left);
