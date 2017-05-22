/*
 * Copyright 2016-2017 Jonathan Eyolfson
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 3 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

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
