#pragma once

#include <stdint.h>

#include "core/report_pipe.h"

void usb_keyboard_init(report_pipe_t *pipe);
void usb_keyboard_task(void);
uint8_t usb_keyboard_led_state(void);

