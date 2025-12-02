#pragma once

void keypad_init();
char keypad_get_key_nonblocking();
bool keypad_check_pin(const char* entered);
