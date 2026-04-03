#pragma once

#include <stdint.h>

enum {
    POWER_CAD_IGNORE = 0,
    POWER_CAD_REBOOT = 1,
};

void power_reboot(void);
void power_poweroff(void);
void power_set_ctrl_alt_del_mode(uint32_t mode);
uint32_t power_get_ctrl_alt_del_mode(void);
