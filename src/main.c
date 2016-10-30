/*
 * Copyright 2016 Jonathan Eyolfson
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

#include <vulkan/vulkan.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int LIBC_ERROR_BIT = 1 << 0;
static const int VULKAN_ERROR_BIT = 1 << 1;
static const int APP_ERROR_BIT = 1 << 2;

int print_result(VkResult result)
{
	const char *msg;
#define PRINT_RESULT_CASE(x) \
case x: \
	msg = #x "\n"; \
	return (size_t) printf("%s", msg) == strlen(msg) ? 0 : LIBC_ERROR_BIT;

	switch (result) {
	PRINT_RESULT_CASE(VK_ERROR_VALIDATION_FAILED_EXT)
	PRINT_RESULT_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
	PRINT_RESULT_CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
	PRINT_RESULT_CASE(VK_ERROR_OUT_OF_DATE_KHR)
	PRINT_RESULT_CASE(VK_ERROR_SURFACE_LOST_KHR)
	PRINT_RESULT_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
	PRINT_RESULT_CASE(VK_ERROR_TOO_MANY_OBJECTS)
	PRINT_RESULT_CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
	PRINT_RESULT_CASE(VK_ERROR_LAYER_NOT_PRESENT)
	PRINT_RESULT_CASE(VK_ERROR_FEATURE_NOT_PRESENT)
	PRINT_RESULT_CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
	PRINT_RESULT_CASE(VK_ERROR_DEVICE_LOST)
	PRINT_RESULT_CASE(VK_ERROR_MEMORY_MAP_FAILED)
	PRINT_RESULT_CASE(VK_ERROR_INITIALIZATION_FAILED)
	PRINT_RESULT_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
	PRINT_RESULT_CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
	PRINT_RESULT_CASE(VK_SUCCESS)
	PRINT_RESULT_CASE(VK_NOT_READY)
	PRINT_RESULT_CASE(VK_TIMEOUT)
	PRINT_RESULT_CASE(VK_EVENT_SET)
	PRINT_RESULT_CASE(VK_EVENT_RESET)
	PRINT_RESULT_CASE(VK_INCOMPLETE)
	PRINT_RESULT_CASE(VK_SUBOPTIMAL_KHR)
#undef PRINT_RESULT_CASE
	default:
		return APP_ERROR_BIT;
	}
}

int use_device(VkDevice device)
{
	return 0;
}

int use_physical_device(VkPhysicalDevice physical_device)
{
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		printf("Using 0: %s\n", properties.deviceName);
	}

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
	};
	VkResult result;
	VkDevice device;
	result = vkCreateDevice(physical_device, &device_create_info, NULL, &device);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	use_device(device);

	vkDestroyDevice(device, NULL);
	return 0;
}

int use_physical_devices(VkPhysicalDevice *physical_devices,
                         uint32_t physical_device_count)
{
	printf("Found %u Physical Devices\n", physical_device_count);

	VkPhysicalDeviceProperties properties;
	for (uint32_t i = 0; i != physical_device_count; ++i) {
		vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
		printf("%u: %s\n", i, properties.deviceName);
	}

	if (physical_device_count == 0) {
		return APP_ERROR_BIT;
	}

	use_physical_device(physical_devices[0]);

	return 0;
}

int use_instance(VkInstance instance)
{
	uint32_t physical_device_count;
	VkResult result;
	result = vkEnumeratePhysicalDevices(
		instance,
		&physical_device_count,
		NULL
	);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}
	VkPhysicalDevice *physical_devices = malloc(
		physical_device_count * sizeof(VkPhysicalDevice)
	);
	if (physical_devices == NULL) {
		return LIBC_ERROR_BIT;
	}
	result = vkEnumeratePhysicalDevices(
		instance,
		&physical_device_count,
		physical_devices
	);
	if (result != VK_SUCCESS) {
		free(physical_devices);
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	use_physical_devices(physical_devices, physical_device_count);

	free(physical_devices);
	return 0;
}

int main(int argc, char **argv)
{
	const char *enabled_extension_names[] = {
		"VK_KHR_surface",
		"VK_KHR_wayland_surface",
	};
	VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.pApplicationInfo = NULL,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = ARRAY_SIZE(enabled_extension_names),
		.ppEnabledExtensionNames = enabled_extension_names,
	};
	VkInstance instance;
	VkResult result;
	result = vkCreateInstance(&instance_create_info, NULL, &instance);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	int ret = use_instance(instance);

	vkDestroyInstance(instance, NULL);
	return ret;
}
