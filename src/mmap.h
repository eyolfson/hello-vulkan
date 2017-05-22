#ifndef HELLO_VULKAN_MMAP
#define HELLO_VULKAN_MMAP

#include <stddef.h>
#include <stdint.h>

struct mmap_result {
	uint32_t *data;
	size_t data_size;
};

uint8_t mmap_init(const char *filename, struct mmap_result *result);
void mmap_fini(struct mmap_result *result);

#endif
