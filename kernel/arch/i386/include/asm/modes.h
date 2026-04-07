#pragma once

#include <stdint.h>

void jump_to_ring3(uint32_t user_eip, uint32_t user_esp, uint32_t user_eflags);
void jump_to_user_image_compat(uint32_t user_eip, uint32_t user_esp);
