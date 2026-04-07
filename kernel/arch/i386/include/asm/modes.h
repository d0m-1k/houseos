#pragma once

#include <stdint.h>

void jump_to_ring3(uint32_t user_eip, uint32_t user_esp, uint32_t user_eflags);
void jump_to_user_image_compat(uint32_t user_eip, uint32_t user_esp);
void jump_to_ring3_state(
    uint32_t user_eip, uint32_t user_esp, uint32_t user_eflags,
    uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
    uint32_t esi, uint32_t edi, uint32_t ebp
);
