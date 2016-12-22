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

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int LIBC_ERROR_BIT = 1 << 0;
static const int VULKAN_ERROR_BIT = 1 << 1;
static const int APP_ERROR_BIT = 1 << 2;
static const int WAYLAND_ERROR_BIT = 1 << 3;

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zxdg_shell_v6 *shell;
	struct wl_surface *surface;
	struct zxdg_surface_v6 *shell_surface;
	struct zxdg_toplevel_v6 *toplevel;
};

static struct wayland wayland = {
	.display = NULL,
	.registry = NULL,
	.compositor = NULL,
	.shell = NULL,
	.surface = NULL,
	.shell_surface = NULL,
};

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
	VkQueue queue;
	/* Assumption: there is only one queue family */
	uint32_t queue_family_index = 0;
	uint32_t queue_index = 0;
	vkGetDeviceQueue(device, queue_family_index, queue_index, &queue);

	return 0;
}

int use_physical_device(VkPhysicalDevice physical_device)
{
	/* Physical Device Queue Family Properties */
	uint32_t queue_family_property_count;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
	                                         &queue_family_property_count,
	                                         NULL);
	VkQueueFamilyProperties *queue_family_properties = malloc(
		queue_family_property_count * sizeof(VkQueueFamilyProperties)
	);
	if (queue_family_properties == NULL) {
		return LIBC_ERROR_BIT;
	}
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
	                                         &queue_family_property_count,
	                                         queue_family_properties);
	if (queue_family_property_count != 1) {
		/* Assumption: there is only one queue family */
		return APP_ERROR_BIT;
	}
	free(queue_family_properties);

	const float queue_priorities[1] = {1.0f};
	VkDeviceQueueCreateInfo device_queue_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		/* Assumption: there is only one queue family */
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = queue_priorities,
	};

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &device_queue_create_info,
	};
	VkResult result;
	VkDevice device;
	result = vkCreateDevice(physical_device,
	                        &device_create_info,
	                        NULL,
	                        &device);
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
	printf("Found %u Physical Device", physical_device_count);
	if (physical_device_count > 1) {
		printf("s");
	}
	printf("\n");

	VkPhysicalDeviceProperties properties;
	for (uint32_t i = 0; i != physical_device_count; ++i) {
		vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
		printf("  %u: %s\n", i, properties.deviceName);
	}

	if (physical_device_count == 0) {
		return APP_ERROR_BIT;
	}

	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical_devices[0], &properties);
		printf("Using 0: %s\n", properties.deviceName);
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

	VkWaylandSurfaceCreateInfoKHR wayland_surface_create_info_khr = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.pNext = NULL,
		.flags = 0,
		.display = wayland.display,
		.surface = wayland.surface,
	};
	VkSurfaceKHR surface_khr;
	result = vkCreateWaylandSurfaceKHR(instance,
	                                   &wayland_surface_create_info_khr,
	                                   NULL,
	                                   &surface_khr);
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

static void wl_registry_global(void *data,
                               struct wl_registry *wl_registry,
                               uint32_t name,
                               const char *interface,
                               uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		wayland.compositor = wl_registry_bind(
			wl_registry, name, &wl_compositor_interface, version);
	}
	else if (strcmp(interface, zxdg_shell_v6_interface.name) == 0) {
		wayland.shell = wl_registry_bind(
			wl_registry, name, &zxdg_shell_v6_interface, version);
	}
}

static void wl_registry_global_remove(void *data,
                                      struct wl_registry *wl_registry,
                                      uint32_t name)
{
	(void) (data);
	(void) (wl_registry);
	(void) (name);
}

static struct wl_registry_listener wl_registry_listener = {
	.global = wl_registry_global,
	.global_remove = wl_registry_global_remove,
};

static void wl_shell_ping(void *data,
                          struct zxdg_shell_v6 *shell,
                          uint32_t serial)
{
	(void) data;

printf("PING\n");
	zxdg_shell_v6_pong(shell, serial);
}

static struct zxdg_shell_v6_listener wl_shell_listener = {
	.ping = wl_shell_ping,
};

static int wayland_init()
{
	wayland.display = wl_display_connect(NULL);
	if (wayland.display == NULL) {
		return WAYLAND_ERROR_BIT;
	}

	wayland.registry = wl_display_get_registry(wayland.display);
	if (wayland.registry == NULL) {
		return WAYLAND_ERROR_BIT;
	}

	wl_registry_add_listener(wayland.registry, &wl_registry_listener, NULL);
	wl_display_roundtrip(wayland.display);
	if ((wayland.compositor == NULL) || (wayland.shell == NULL)) {
		return WAYLAND_ERROR_BIT;
	}

	zxdg_shell_v6_add_listener(wayland.shell, &wl_shell_listener, NULL);

	wayland.surface = wl_compositor_create_surface(wayland.compositor);
	if (wayland.surface == NULL) {
		return WAYLAND_ERROR_BIT;
	}

	wayland.shell_surface = zxdg_shell_v6_get_xdg_surface(wayland.shell,
	                                                      wayland.surface);
	if (wayland.shell_surface == NULL) {
		return WAYLAND_ERROR_BIT;
	}

	wayland.toplevel = zxdg_surface_v6_get_toplevel(wayland.shell_surface);
	if (wayland.toplevel == NULL) {
		return WAYLAND_ERROR_BIT;
	}

	zxdg_surface_v6_set_window_geometry(wayland.shell_surface,
	                                    0, 0, 640, 480);
	zxdg_toplevel_v6_set_title(wayland.toplevel, "Hello Vulkan");

	return 0;
}

static void wayland_fini()
{
	if (wayland.toplevel != NULL) {
		zxdg_toplevel_v6_destroy(wayland.toplevel);
	}
	if (wayland.shell_surface != NULL) {
		zxdg_surface_v6_destroy(wayland.shell_surface);
	}
	if (wayland.surface != NULL) {
		wl_surface_destroy(wayland.surface);
	}
	if (wayland.shell != NULL) {
		zxdg_shell_v6_destroy(wayland.shell);
	}
	if (wayland.compositor != NULL) {
		wl_compositor_destroy(wayland.compositor);
	}
	if (wayland.registry != NULL) {
		wl_registry_destroy(wayland.registry);
	}
	if (wayland.display != NULL) {
		wl_display_disconnect(wayland.display);
	}
}

int main(int argc, char **argv)
{
	int ret = wayland_init();
	if (ret != 0) {
		return ret;
	}

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
		ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	ret = use_instance(instance);

	vkDestroyInstance(instance, NULL);
	wayland_fini();
	return ret;
}
