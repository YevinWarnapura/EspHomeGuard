#include "keypad.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "KEYPAD";

#define ROWS 4
#define COLS 4

// ============================
// CORRECTED PIN MAPPING
// (Matches your physical wiring)
// ============================

// ROW pins (outputs)
static gpio_num_t rowPins[ROWS] = {
    GPIO_NUM_13,  // R1
    GPIO_NUM_12,  // R2
    GPIO_NUM_14,  // R3
    GPIO_NUM_27   // R4
};

// COLUMN pins (inputs)
static gpio_num_t colPins[COLS] = {
    GPIO_NUM_26,  // C1
    GPIO_NUM_25,  // C2
    GPIO_NUM_33,  // C3
    GPIO_NUM_32   // C4
};

// Key layout
static const char keymap[ROWS][COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};


// ============================
// PIN STORAGE (NVS)
// ============================

static char g_pin[5] = "1234";

static void load_pin_from_nvs() {

    nvs_handle_t h;
    esp_err_t err = nvs_open("alarm", NVS_READWRITE, &h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return;
    }

    size_t len = sizeof(g_pin);
    err = nvs_get_str(h, "pin", g_pin, &len);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "PIN not found → writing default 1234");
        nvs_set_str(h, "pin", g_pin);
        nvs_commit(h);
    }

    nvs_close(h);
}

bool keypad_check_pin(const char* entered) {
    return strncmp(entered, g_pin, 4) == 0;
}


// ============================
// INIT
// ============================

void keypad_init() {
    ESP_LOGI(TAG, "Init keypad...");

    static bool nvs_done = false;
    if (!nvs_done) {
        nvs_flash_init();
        nvs_done = true;
    }

    load_pin_from_nvs();

    // ROWS = OUTPUT (idle HIGH)
    for (int r = 0; r < ROWS; r++) {
        gpio_set_direction(rowPins[r], GPIO_MODE_OUTPUT);
        gpio_set_level(rowPins[r], 1);
    }

    // COLS = INPUT WITH PULLUP
    for (int c = 0; c < COLS; c++) {
        gpio_set_direction(colPins[c], GPIO_MODE_INPUT);
        gpio_pullup_en(colPins[c]);
    }

    ESP_LOGI(TAG, "Keypad ready");
}


// ============================
// NON-BLOCKING SCAN
// ============================

char keypad_get_key_nonblocking() {

    for (int r = 0; r < ROWS; r++) {

        gpio_set_level(rowPins[r], 0);  // activate this row
        vTaskDelay(pdMS_TO_TICKS(1));

        for (int c = 0; c < COLS; c++) {

            if (gpio_get_level(colPins[c]) == 0) {

                // debounce: wait until release
                vTaskDelay(pdMS_TO_TICKS(20));
                while (gpio_get_level(colPins[c]) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }

                gpio_set_level(rowPins[r], 1);
                return keymap[r][c];
            }
        }

        gpio_set_level(rowPins[r], 1);  // deactivate row
    }

    return 0;
}
