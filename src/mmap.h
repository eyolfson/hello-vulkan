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

#ifndef HELLO_VULKAN_MMAP_H
#define HELLO_VULKAN_MMAP_H

#include <stddef.h>
#include <stdint.h>

struct mmap_result {
	uint32_t *data;
	size_t data_size;
};

uint8_t mmap_init(const char *filename, struct mmap_result *result);
void mmap_fini(struct mmap_result *result);

#endif
