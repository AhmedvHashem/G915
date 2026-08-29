#pragma once

#include <stddef.h>
#include <stdint.h>

void bridge_keyboard_report(const uint8_t *report, size_t length);
void bridge_keyboard_connected(void);
void bridge_keyboard_disconnected(void);

