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
#include "led.h"
#include "remote.h"

// ==== NEW INCLUDES FOR WIFI + MQTT ====
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "lwip/err.h"
#include "lwip/sys.h"

// =========================
// Global logging tag
// =========================
static const char* TAG = "ALARM_MAIN";

// =========================
// WiFi + MQTT config
// =========================
#define WIFI_SSID "NOKIA-1580"
#define WIFI_PASS "unitthree"

// EMQX Serverless TLS endpoint
static const char* MQTT_URI = "mqtts://s66a1a0e.ala.us-east-1.emqxsl.com:8883";

// MQTT topics
static const char* TOPIC_CMD       = "alarm/cmd";
static const char* TOPIC_TELEMETRY = "alarm/telemetry";

// CA certificate from emqxsl-ca.crt
// Paste entire file contents (including BEGIN/END lines) inside the string:
static const char EMQX_CA_CERT_PEM[] = R"(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----)";

// MQTT client handle
static esp_mqtt_client_handle_t g_mqtt_client = nullptr;


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

// last ultrasonic distance (for telemetry)
static int g_last_distance_cm = -1;


// =========================
// Forward declarations
// =========================

void alarm_task(void* pv);
void ultrasonic_task(void* pv);
void keypad_task(void* pv);
void speaker_task(void* pv);
void led_task(void* pv);
// remote_task is no longer used for MQTT-based remote, but we keep it if
// you want to simulate IR remote etc.
void remote_task(void* pv);
void lcd_task(void* pv);
void mqtt_task(void* pv);  // NEW


// =========================
// Abstract APIs
// =========================

void led_set_disarmed();
void led_set_armed();
void led_set_alarm();
void led_set_exit_delay_level(int sec_left);

RemoteCommandType remote_check_command();


// =========================
// WIFI INIT (STA MODE)
// =========================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected + got IP");
    }
}

static void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA init done");
}


// =========================
// MQTT EVENT HANDLER
// =========================

static void mqtt_publish_state()
{
    if (!g_mqtt_client) return;

    AlarmState s = g_state;
    const char* state_str = "DISARMED";
    switch (s) {
        case AlarmState::DISARMED:  state_str = "DISARMED";  break;
        case AlarmState::EXIT_DELAY:state_str = "EXIT_DELAY";break;
        case AlarmState::ARMED:     state_str = "ARMED";     break;
        case AlarmState::ALARM:     state_str = "ALARM";     break;
    }

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\",\"distance_cm\":%d}",
             state_str, g_last_distance_cm);

    int msg_id = esp_mqtt_client_publish(
        g_mqtt_client, TOPIC_TELEMETRY, payload, 0, 1, 0);

    ESP_LOGI(TAG, "MQTT publish telemetry msg_id=%d: %s", msg_id, payload);
}

static void mqtt_arm_disarm_from_cmd(const char* cmd, int len)
{
    std::string c(cmd, cmd + len);

    if (c == "ARM") {
        AlarmEvent ev{ AlarmEventType::ARM_REMOTE };
        xQueueSend(g_eventQueue, &ev, 0);
        ESP_LOGI(TAG, "MQTT: ARM command received");
    } else if (c == "DISARM") {
        AlarmEvent ev{ AlarmEventType::DISARM_REMOTE };
        xQueueSend(g_eventQueue, &ev, 0);
        ESP_LOGI(TAG, "MQTT: DISARM command received");
    } else {
        ESP_LOGW(TAG, "MQTT: Unknown cmd '%s'", c.c_str());
    }
}

static void mqtt_event_handler(void* handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void* event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(g_mqtt_client, TOPIC_CMD, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA: topic=%.*s data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            // check if this is the command topic
            std::string topic(event->topic, event->topic + event->topic_len);
            if (topic == TOPIC_CMD) {
                mqtt_arm_disarm_from_cmd(event->data, event->data_len);
            }
            break;
        }

        default:
            break;
    }
}

static void mqtt_init()
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_URI;
    mqtt_cfg.credentials.username = "homeGuard";
    mqtt_cfg.credentials.authentication.password = "gurrKash67cutwater"; 
    mqtt_cfg.broker.verification.certificate = EMQX_CA_CERT_PEM;

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        g_mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
        mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt_client));

    ESP_LOGI(TAG, "MQTT client started");
}


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

            if (old != g_state) {
                ESP_LOGI(TAG, "STATE CHANGE: %d -> %d",
                         (int)old, (int)g_state);
                // whenever state changes, push telemetry
                mqtt_publish_state();
            }
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
                mqtt_publish_state();
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
        g_last_distance_cm = dist_cm;  // for telemetry

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
// (unchanged except uses event queue)
// =========================

// ... keep your existing keypad_task exactly as in original file ...

// [FOR BREVITY: paste your existing keypad_task implementation here,
// it’s unchanged from your current code.]

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

            if (key == ARM_KEY && !entering_pin)
            {
                AlarmEvent ev{ AlarmEventType::ARM_LOCAL };
                xQueueSend(g_eventQueue, &ev, 0);
                continue;
            }

            if (!entering_pin)
            {
                entering_pin = true;
                idx = 0;
                memset(buffer, 0, sizeof(buffer));

                lcd_clear();
                lcd_show_message("ENTER PIN:");
                lcd_set_cursor(0, 1);
                lcd_write_string("    ");
            }

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

                entering_pin = false;
                idx = 0;
                memset(buffer, 0, sizeof(buffer));
                continue;
            }

            if (key >= '0' && key <= '9')
            {
                if (idx < 4)
                {
                    buffer[idx++] = key;

                    char stars[5] = "    ";
                    for (int i = 0; i < idx; i++)
                        stars[i] = '*';

                    lcd_clear();
                    lcd_show_message("ENTER PIN:");
                    lcd_set_cursor(0, 1);
                    lcd_write_string(stars);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}


// =========================
// Speaker Task (unchanged)
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
// LED Task (unchanged)
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
// MQTT Task
// Periodically publishes telemetry
// =========================

void mqtt_task(void* pv)
{
    while (true)
    {
        mqtt_publish_state();
        vTaskDelay(pdMS_TO_TICKS(2000));  // every 2s
    }
}


// =========================
// Remote Task (optional stub)
// =========================

void remote_task(void* pv)
{
    while (true)
    {
        // keep if you'd like to still support the old stub IR remote.
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
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Smart Home Alarm – RTOS core starting");

    wifi_init_sta();  // NEW
    vTaskDelay(pdMS_TO_TICKS(2000)); // give WiFi time

    mqtt_init();      // NEW

    ultrasonic_init();
    keypad_init();
    lcd_init();
    speaker_init();
    led_init();

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
    xTaskCreate(mqtt_task,      "mqtt_task",      4096, nullptr, 4,  nullptr);
    xTaskCreate(remote_task,    "remote_task",    2048, nullptr, 3,  nullptr);
    xTaskCreate(lcd_task,       "lcd_task",       2048, nullptr, 2,  nullptr);

    ESP_LOGI(TAG, "RTOS core running.");
}
