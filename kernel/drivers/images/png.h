#pragma once

#include <drivers/images/image.h>

bool png_verify(const uint8_t* data, size_t size);
image_t* png_load(const uint8_t* data, size_t size);