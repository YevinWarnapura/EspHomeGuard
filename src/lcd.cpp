// src/lcd.cpp

#include "lcd.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* TAG_LCD = "LCD_I2C";

// Global mutex for LCD access
SemaphoreHandle_t lcd_mutex = nullptr;

// Simple macros to lock/unlock
#define LCD_LOCK()   do { if (lcd_mutex) xSemaphoreTake(lcd_mutex, portMAX_DELAY); } while (0)
#define LCD_UNLOCK() do { if (lcd_mutex) xSemaphoreGive(lcd_mutex); } while (0)

// -------------------------
// I2C configuration
// -------------------------
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_PIN     GPIO_NUM_21   // change if wired differently
#define I2C_SCL_PIN     GPIO_NUM_22   // change if wired differently
#define I2C_FREQ_HZ     100000
#define LCD_I2C_ADDR    0x27          // common PCF8574 address (0x27 or 0x3F)

// -------------------------
// PCF8574 pin mapping
// -------------------------
//
// P0 -> RS
// P1 -> RW
// P2 -> EN
// P3 -> BACKLIGHT
// P4 -> D4
// P5 -> D5
// P6 -> D6
// P7 -> D7
//
#define LCD_RS  (1 << 0)
#define LCD_RW  (1 << 1)
#define LCD_EN  (1 << 2)
#define LCD_BL  (1 << 3)
#define LCD_D4  (1 << 4)
#define LCD_D5  (1 << 5)
#define LCD_D6  (1 << 6)
#define LCD_D7  (1 << 7)

static bool s_backlight = true;

// -------------------------
// Low-level I2C write
// -------------------------

static esp_err_t lcd_i2c_write_byte(uint8_t data)
{
    return i2c_master_write_to_device(
        I2C_PORT,
        LCD_I2C_ADDR,
        &data,
        1,
        pdMS_TO_TICKS(50)
    );
}

// -------------------------
// Send a 4-bit nibble to LCD (upper or lower part of a byte)
// rs = 0 for command, 1 for data
// -------------------------

static void lcd_write_nibble(uint8_t nibble, bool rs)
{
    uint8_t data = 0;

    if (rs)          data |= LCD_RS;
    if (s_backlight) data |= LCD_BL;

    if (nibble & 0x01) data |= LCD_D4;
    if (nibble & 0x02) data |= LCD_D5;
    if (nibble & 0x04) data |= LCD_D6;
    if (nibble & 0x08) data |= LCD_D7;

    // EN HIGH
    data |= LCD_EN;
    lcd_i2c_write_byte(data);
    vTaskDelay(pdMS_TO_TICKS(1));

    // EN LOW
    data &= ~LCD_EN;
    lcd_i2c_write_byte(data);
    vTaskDelay(pdMS_TO_TICKS(1));
}

// -------------------------
// Send full byte (command or data) using 4-bit interface
// -------------------------

static void lcd_send_cmd(uint8_t cmd)
{
    // upper nibble first
    lcd_write_nibble((cmd >> 4) & 0x0F, false);
    // lower nibble
    lcd_write_nibble(cmd & 0x0F, false);
}

static void lcd_send_data(uint8_t data)
{
    lcd_write_nibble((data >> 4) & 0x0F, true);
    lcd_write_nibble(data & 0x0F, true);
}

// -------------------------
// Public functions
// -------------------------

void lcd_init()
{
    ESP_LOGI(TAG_LCD, "Initializing I2C LCD...");

    // Create mutex once
    if (lcd_mutex == nullptr) {
        lcd_mutex = xSemaphoreCreateMutex();
        if (!lcd_mutex) {
            ESP_LOGE(TAG_LCD, "Failed to create LCD mutex!");
        }
    }

    // I2C config
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA_PIN;
    conf.scl_io_num = I2C_SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FREQ_HZ;

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));

    vTaskDelay(pdMS_TO_TICKS(50)); // wait for LCD power-up

    // We don't lock here because no other task uses LCD before init completes

    // Initialization sequence for HD44780 in 4-bit mode
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x02, false);  // 4-bit mode

    // Function set: 4-bit, 2 line, 5x8 dots
    lcd_send_cmd(0x28);
    // Display off
    lcd_send_cmd(0x08);
    // Clear display
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    // Entry mode: increment, no shift
    lcd_send_cmd(0x06);
    // Display on, cursor off, blink off
    lcd_send_cmd(0x0C);

    ESP_LOGI(TAG_LCD, "LCD init done");
}

void lcd_clear()
{
    LCD_LOCK();
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    LCD_UNLOCK();
}

void lcd_set_cursor(int col, int row)
{
    // ⚠ Not locked: used inside already-locked high-level functions and by keypad
    if (col < 0) col = 0;
    if (col > 15) col = 15;
    if (row < 0) row = 0;
    if (row > 1) row = 1;

    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    addr += col;
    lcd_send_cmd(0x80 | addr);
}

void lcd_write_char(char c)
{
    // ⚠ Not locked: assume caller handles concurrency
    lcd_send_data((uint8_t)c);
}

void lcd_write_string(const char* str)
{
    // ⚠ Not locked: assume caller handles concurrency
    while (*str) {
        lcd_write_char(*str++);
    }
}

// High-level helper: show text on both lines, safely
void lcd_show_message(const char* msg)
{
    LCD_LOCK();

    // Clear display
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(3));

    // Write line 1
    lcd_set_cursor(0, 0);

    int i = 0;
    while (msg[i] != '\0' && msg[i] != '\n') {
        lcd_write_char(msg[i]);
        i++;
    }

    // Clear rest of line 1
    for (int col = i; col < 16; col++) {
        lcd_write_char(' ');
    }

    // Move to second line
    lcd_set_cursor(0, 1);

    // If no newline, clear line 2 and exit
    if (msg[i] != '\n') {
        lcd_write_string("                "); // 16 spaces
        LCD_UNLOCK();
        return;
    }

    // Has newline → print line 2
    i++; // skip newline
    int col = 0;

    while (msg[i] != '\0' && col < 16) {
        lcd_write_char(msg[i]);
        i++;
        col++;
    }

    // Clear rest of line 2
    for (; col < 16; col++) lcd_write_char(' ');

    LCD_UNLOCK();
}

// High-level helper: show countdown on second line
void lcd_show_countdown(int seconds_left)
{
    char buf[17] = {0};
    snprintf(buf, sizeof(buf), "EXIT: %2ds", seconds_left);

    LCD_LOCK();

    lcd_set_cursor(0, 1);
    lcd_write_string("                "); // clear whole line
    lcd_set_cursor(0, 1);
    lcd_write_string(buf);

    LCD_UNLOCK();
}
