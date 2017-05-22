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
