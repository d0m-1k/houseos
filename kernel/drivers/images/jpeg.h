#pragma once

#include <drivers/images/image.h>

bool jpeg_verify(const uint8_t* data, size_t size);
image_t* jpeg_load(const uint8_t* data, size_t size);