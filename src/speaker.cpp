#include "speaker.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "SPEAKER";

#define SPEAKER_PIN GPIO_NUM_17

// Tone settings
static const int ALARM_FREQ = 2000;  // 2 kHz continuous alarm tone
static const int BEEP_FREQ  = 1500;  // single beep tone
static const int PWM_DUTY   = 400;   // duty cycle out of 8191 (≈5%)

static bool alarm_active = false;
static int64_t beep_end_time = 0;

// ===============================
// Initialize PWM on SPEAKER_PIN
// ===============================
void speaker_init()
{
    ESP_LOGI(TAG, "Initializing speaker...");

    ledc_timer_config_t timer = {};
    timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution  = LEDC_TIMER_13_BIT;
    timer.timer_num        = LEDC_TIMER_0;
    timer.freq_hz          = ALARM_FREQ;
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {};
    channel.gpio_num       = SPEAKER_PIN;
    channel.speed_mode     = LEDC_LOW_SPEED_MODE;
    channel.channel        = LEDC_CHANNEL_0;
    channel.timer_sel      = LEDC_TIMER_0;
    channel.duty           = 0;         // start silent
    ledc_channel_config(&channel);

    ESP_LOGI(TAG, "Speaker ready!");
}

// ===============================
// Continuous Alarm Tone
// ===============================
void speaker_set_alarm(bool on)
{
    alarm_active = on;

    if (on) {
        // Set PWM frequency & start tone
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, ALARM_FREQ);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, PWM_DUTY);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "ALARM ON");
    } else {
        // Stop tone
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "ALARM OFF");
    }
}

// ===============================
// Non-blocking short beep
// speaker_task polls this every 20ms
// ===============================
void speaker_beep_once(int ms)
{
    int64_t now = esp_timer_get_time();
    beep_end_time = now + (ms * 1000);

    // set beep freq
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, BEEP_FREQ);

    // turn ON beep
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, PWM_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGI(TAG, "Beep start (%d ms)", ms);
}

// ===============================
// Called periodically by speaker_task
// Turns beep OFF at correct time
// ===============================
void speaker_update()
{
    if (alarm_active) return; // continuous alarm overrides beeps

    int64_t now = esp_timer_get_time();
    if (beep_end_time != 0 && now >= beep_end_time) {

        // stop beep
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        beep_end_time = 0;
        ESP_LOGI(TAG, "Beep end");
    }
}
