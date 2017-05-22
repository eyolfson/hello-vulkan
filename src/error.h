#ifndef HELLO_VULKAN_ERROR_H
#define HELLO_VULKAN_ERROR_H

#include <stdint.h>

static const uint8_t NO_ERRORS = 0;
static const uint8_t LIBC_ERROR_BIT = 1 << 0;
static const uint8_t VULKAN_ERROR_BIT = 1 << 1;
static const uint8_t APP_ERROR_BIT = 1 << 2;
static const uint8_t WAYLAND_ERROR_BIT = 1 << 3;
static const uint8_t POSIX_ERROR_BIT = 1 << 4;

#endif
