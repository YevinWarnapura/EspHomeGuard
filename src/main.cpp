// src/main.cpp

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <cstring>
#include <string>
#include "nvs_flash.h"

#include "lcd.h"
#include "ultrasonic.h"
#include "keypad.h"
#include "speaker.h"
#include "led.h"        // <-- ADDED
#include "remote.h"     // <-- you will add later (stub is fine)


// =========================
// Global logging tag
// =========================
static const char* TAG = "ALARM_MAIN";


// =========================
// Alarm States & Events
// =========================

enum class AlarmState {
    DISARMED,
    EXIT_DELAY,
    ARMED,
    ALARM
};

enum class AlarmEventType {
    ARM_LOCAL,
    ARM_REMOTE,
    DISARM_PIN_OK,
    DISARM_OVERRIDE,
    DISARM_REMOTE,
    MOTION_DETECTED,
    RESET
};

struct AlarmEvent {
    AlarmEventType type;
};


// =========================
// Global State
// =========================
static AlarmState g_state = AlarmState::DISARMED;
static QueueHandle_t g_eventQueue = nullptr;

static const int EXIT_DELAY_MS = 15000;
static TickType_t g_exit_deadline = 0;
static int g_exit_seconds_remaining = 0;


// =========================
// Forward declarations
// =========================

void alarm_task(void* pv);
void ultrasonic_task(void* pv);
void keypad_task(void* pv);
void speaker_task(void* pv);
void led_task(void* pv);
void remote_task(void* pv);
void lcd_task(void* pv);


// =========================
// Abstract APIs
// =========================

void led_set_disarmed();
void led_set_armed();
void led_set_alarm();
void led_set_exit_delay_level(int sec_left);

RemoteCommandType remote_check_command();


// =========================
// Alarm State Machine
// =========================

void alarm_task(void* pv)
{
    AlarmEvent ev;

    while (true)
    {
        if (xQueueReceive(g_eventQueue, &ev, pdMS_TO_TICKS(100)))
        {
            AlarmState old = g_state;

            switch (g_state)
            {
                case AlarmState::DISARMED:
                    if (ev.type == AlarmEventType::ARM_LOCAL ||
                        ev.type == AlarmEventType::ARM_REMOTE)
                    {
                        g_state = AlarmState::EXIT_DELAY;
                        TickType_t now = xTaskGetTickCount();

                        g_exit_deadline = now + pdMS_TO_TICKS(EXIT_DELAY_MS);
                        g_exit_seconds_remaining = EXIT_DELAY_MS / 1000;

                        lcd_show_message("EXIT DELAY");
                        ESP_LOGI(TAG, "Exit delay started");
                    }
                    break;


                case AlarmState::EXIT_DELAY:
                    if (ev.type == AlarmEventType::DISARM_PIN_OK ||
                        ev.type == AlarmEventType::DISARM_OVERRIDE ||
                        ev.type == AlarmEventType::DISARM_REMOTE)
                    {
                        g_state = AlarmState::DISARMED;
                        lcd_show_message("DISARMED");
                        ESP_LOGI(TAG, "Exit delay cancelled");
                    }
                    break;


                case AlarmState::ARMED:
                    if (ev.type == AlarmEventType::MOTION_DETECTED)
                    {
                        g_state = AlarmState::ALARM;
                        lcd_show_message("ALARM TRIGGERED");
                        ESP_LOGI(TAG, "Motion → ALARM");
                    }

                    if (ev.type == AlarmEventType::DISARM_PIN_OK ||
                        ev.type == AlarmEventType::DISARM_OVERRIDE ||
                        ev.type == AlarmEventType::DISARM_REMOTE)
                    {
                        g_state = AlarmState::DISARMED;
                        lcd_show_message("DISARMED");
                    }
                    break;


                case AlarmState::ALARM:
                    if (ev.type == AlarmEventType::DISARM_PIN_OK ||
                        ev.type == AlarmEventType::DISARM_OVERRIDE ||
                        ev.type == AlarmEventType::DISARM_REMOTE ||
                        ev.type == AlarmEventType::RESET)
                    {
                        g_state = AlarmState::DISARMED;
                        lcd_show_message("DISARMED");
                    }
                    break;
            }

            if (old != g_state)
                ESP_LOGI(TAG, "STATE CHANGE: %d -> %d", (int)old, (int)g_state);
        }


        // TIMER HANDLING FOR EXIT DELAY
        if (g_state == AlarmState::EXIT_DELAY)
        {
            TickType_t now = xTaskGetTickCount();

            if (now >= g_exit_deadline)
            {
                g_state = AlarmState::ARMED;
                lcd_show_message("ARMED");
                g_exit_seconds_remaining = 0;
                ESP_LOGI(TAG, "System ARMED");
            }
            else
            {
                int sec_left = (g_exit_deadline - now) / configTICK_RATE_HZ;

                if (sec_left != g_exit_seconds_remaining)
                {
                    g_exit_seconds_remaining = sec_left;
                    lcd_show_countdown(sec_left);
                }
            }
        }
    }
}


// =========================
// Ultrasonic Task
// =========================

void ultrasonic_task(void* pv)
{
    while (true)
    {
        int dist_cm = ultrasonic_get_distance_cm();

        if (dist_cm > 0 && dist_cm <= 100)
        {
            AlarmEvent ev{ AlarmEventType::MOTION_DETECTED };
            xQueueSend(g_eventQueue, &ev, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}


// =========================
// Keypad Task
// =========================

// =========================
// Keypad Task
// =========================

void keypad_task(void* pv)
{
    char buffer[5] = {0};
    int idx = 0;
    bool entering_pin = false;

    const char ARM_KEY = 'A';

    while (true)
    {
        char key = keypad_get_key_nonblocking();

        if (key != 0)
        {
            ESP_LOGI("KEYPAD", "Key: %c", key);

            // ================================
            // ARM SYSTEM (A KEY) when not entering PIN
            // ================================
            if (key == ARM_KEY && !entering_pin)
            {
                AlarmEvent ev{ AlarmEventType::ARM_LOCAL };
                xQueueSend(g_eventQueue, &ev, 0);
                // You can also give a message here if you want:
                // lcd_show_message("EXIT DELAY");
                continue;
            }

            // If we get here and we're not in PIN mode yet, enter it
            if (!entering_pin)
            {
                entering_pin = true;
                idx = 0;
                memset(buffer, 0, sizeof(buffer));

                lcd_clear();
                lcd_show_message("ENTER PIN:");
                lcd_set_cursor(0, 1);
                lcd_write_string("    ");   // clear 2nd line
            }

            // ================================
            // CLEAR WITH *
            // ================================
            if (key == '*')
            {
                idx = 0;
                memset(buffer, 0, sizeof(buffer));

                lcd_clear();
                lcd_show_message("ENTER PIN:");
                lcd_set_cursor(0, 1);
                lcd_write_string("    ");
                continue;
            }

            // ================================
            // ENTER WITH #
            // ================================
            if (key == '#')
            {
                if (idx == 4)
                {
                    if (keypad_check_pin(buffer))
                    {
                        AlarmEvent ev{ AlarmEventType::DISARM_PIN_OK };
                        xQueueSend(g_eventQueue, &ev, 0);

                        lcd_clear();
                        lcd_show_message("DISARMED");
                    }
                    else
                    {
                        lcd_clear();
                        lcd_show_message("WRONG PIN");
                        vTaskDelay(pdMS_TO_TICKS(1000));

                        lcd_clear();
                        lcd_show_message("ENTER PIN:");
                        lcd_set_cursor(0, 1);
                        lcd_write_string("    ");
                    }
                }
                else
                {
                    lcd_clear();
                    lcd_show_message("NEED 4 DIGITS");
                    vTaskDelay(pdMS_TO_TICKS(700));

                    lcd_clear();
                    lcd_show_message("ENTER PIN:");
                    lcd_set_cursor(0, 1);
                    lcd_write_string("    ");
                }

                // reset after pressing #
                entering_pin = false;
                idx = 0;
                memset(buffer, 0, sizeof(buffer));
                continue;
            }

            // ================================
            // DIGITS ONLY
            // ================================
            if (key >= '0' && key <= '9')
            {
                if (idx < 4)
                {
                    buffer[idx++] = key;

                    // build stars string
                    char stars[5] = "    ";   // 4 chars + null
                    for (int i = 0; i < idx; i++)
                        stars[i] = '*';

                    lcd_clear();
                    lcd_show_message("ENTER PIN:");
                    lcd_set_cursor(0, 1);      // second line
                    lcd_write_string(stars);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}



// =========================
// Speaker Task
// =========================

extern void speaker_update();

void speaker_task(void* pv)
{
    TickType_t last_beep = xTaskGetTickCount();

    while (true)
    {
        speaker_update();

        AlarmState s = g_state;
        TickType_t now = xTaskGetTickCount();

        if (s == AlarmState::ALARM)
        {
            speaker_set_alarm(true);
        }
        else if (s == AlarmState::EXIT_DELAY)
        {
            speaker_set_alarm(false);

            int sec_left = g_exit_seconds_remaining;
            int interval = 800;

            if (sec_left <= 10 && sec_left > 5) interval = 400;
            else if (sec_left <= 5) interval = 150;

            if (now - last_beep >= pdMS_TO_TICKS(interval))
            {
                last_beep = now;
                speaker_beep_once(80);
            }
        }
        else
        {
            speaker_set_alarm(false);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


// =========================
// LED Task
// =========================

void led_task(void* pv)
{
    AlarmState prev = AlarmState::DISARMED;
    int prev_sec = -1;

    while (true)
    {
        AlarmState s = g_state;

        if (s != prev)
        {
            prev = s;

            switch (s)
            {
                case AlarmState::DISARMED: led_set_disarmed(); break;
                case AlarmState::ARMED:    led_set_armed();    break;
                case AlarmState::ALARM:    led_set_alarm();    break;
                default: break;
            }
        }

        if (s == AlarmState::EXIT_DELAY)
        {
            if (prev_sec != g_exit_seconds_remaining)
            {
                prev_sec = g_exit_seconds_remaining;
                led_set_exit_delay_level(prev_sec);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// =========================
// Remote Task
// =========================

void remote_task(void* pv)
{
    while (true)
    {
        RemoteCommandType cmd = remote_check_command();

        if (cmd == RemoteCommandType::ARM)
        {
            AlarmEvent ev{ AlarmEventType::ARM_REMOTE };
            xQueueSend(g_eventQueue, &ev, 0);
        }
        else if (cmd == RemoteCommandType::DISARM)
        {
            AlarmEvent ev{ AlarmEventType::DISARM_REMOTE };
            xQueueSend(g_eventQueue, &ev, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


// =========================
// LCD Task
// =========================

void lcd_task(void* pv)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// =========================
// MAIN ENTRY
// =========================

extern "C" void app_main(void)
{

    nvs_flash_erase();
    nvs_flash_init();

    ESP_LOGI(TAG, "Smart Home Alarm – RTOS core starting");

    ultrasonic_init();
    keypad_init();
    lcd_init();
    speaker_init();

    led_init();    // <-- REQUIRED FOR LED DRIVER

    g_eventQueue = xQueueCreate(16, sizeof(AlarmEvent));
    if (!g_eventQueue)
    {
        ESP_LOGE(TAG, "Event queue creation failed");
        return;
    }

    xTaskCreate(alarm_task,     "alarm_task",     4096, nullptr, 10, nullptr);
    xTaskCreate(ultrasonic_task,"ultra_task",     2048, nullptr, 8,  nullptr);
    xTaskCreate(keypad_task,    "keypad_task",    4096, nullptr, 7,  nullptr);
    xTaskCreate(speaker_task,   "speaker_task",   2048, nullptr, 6,  nullptr);
    xTaskCreate(led_task,       "led_task",       2048, nullptr, 5,  nullptr);
    xTaskCreate(remote_task,    "remote_task",    2048, nullptr, 4,  nullptr);
    xTaskCreate(lcd_task,       "lcd_task",       2048, nullptr, 3,  nullptr);

    ESP_LOGI(TAG, "RTOS core running.");
}
