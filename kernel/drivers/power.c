#include <drivers/power.h>
#include <asm/port.h>
#include <asm/processor.h>

static uint32_t g_cad_mode = POWER_CAD_REBOOT;

void power_set_ctrl_alt_del_mode(uint32_t mode) {
    g_cad_mode = (mode == POWER_CAD_IGNORE) ? POWER_CAD_IGNORE : POWER_CAD_REBOOT;
}

uint32_t power_get_ctrl_alt_del_mode(void) {
    return g_cad_mode;
}

void power_reboot(void) {
    cli();
    outb(0x64, 0xFE);
    while (1) hlt();
}

void power_poweroff(void) {
    cli();

    
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    while (1) hlt();
}
