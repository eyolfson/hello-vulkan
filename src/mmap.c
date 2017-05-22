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

#include "mmap.h"

#include "error.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

uint8_t mmap_init(const char *filename, struct mmap_result *result)
{
	int fd = open(filename, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		return POSIX_ERROR_BIT;
	}

	struct stat stat;
	if (fstat(fd, &stat) == -1) {
		close(fd);
		return POSIX_ERROR_BIT;
	}

	result->data_size = stat.st_size;
	result->data = mmap(NULL, result->data_size, PROT_READ, MAP_PRIVATE,
	                    fd, 0);

	if (result->data == MAP_FAILED) {
		close(fd);
		return POSIX_ERROR_BIT;
	}

	close(fd);
	return 0;
}

void mmap_fini(struct mmap_result *result)
{
	munmap((void *) result->data, result->data_size);
	result->data = NULL;
	result->data_size = 0;
}
