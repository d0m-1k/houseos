#pragma once

#include <stdint.h>

void timer_init(void);
void timer_handler(void);
uint32_t timer_get_ticks(void);
void sleep(uint32_t ms);