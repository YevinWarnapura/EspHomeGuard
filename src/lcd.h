// include/lcd.h
#pragma once

// Initialize I2C and the LCD (4-bit mode, 2-line)
void lcd_init();

// Clear screen
void lcd_clear();

// Set cursor position (0-based: col 0–15, row 0–1)
void lcd_set_cursor(int col, int row);

// Write a single character at current cursor
void lcd_write_char(char c);

// Write a null-terminated string starting at current cursor
void lcd_write_string(const char* str);

// High-level helpers used by main.cpp
void lcd_show_message(const char* msg);      // show on first line
void lcd_show_countdown(int seconds_left);   // show on second line
