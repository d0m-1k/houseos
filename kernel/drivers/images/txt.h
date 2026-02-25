#pragma once

#include <drivers/images/image.h>

bool txt_verify(const uint8_t* data, size_t size);
image_t* txt_load(const uint8_t* data, size_t size);
