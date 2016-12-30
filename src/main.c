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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

static const int16_t WIDTH = 640;
static const int16_t HEIGHT = 480;

static VkQueue queue;
static VkSwapchainKHR swapchain;

static uint32_t min_image_count;
static VkSurfaceTransformFlagBitsKHR current_transform;

static VkExtent2D swapchain_image_extent = {
	.width = WIDTH,
	.height = HEIGHT
};
static VkFormat swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

static const uint8_t LIBC_ERROR_BIT = 1 << 0;
static const uint8_t VULKAN_ERROR_BIT = 1 << 1;
static const uint8_t APP_ERROR_BIT = 1 << 2;
static const uint8_t WAYLAND_ERROR_BIT = 1 << 3;
static const uint8_t POSIX_ERROR_BIT = 1 << 4;

static uint8_t mmap_file(const char *filename, const uint32_t **code, size_t *code_size)
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

	*code_size = stat.st_size;
	*code = mmap(NULL, *code_size, PROT_READ, MAP_PRIVATE, fd, 0);

	if (*code == MAP_FAILED) {
		close(fd);
		return POSIX_ERROR_BIT;
	}

	close(fd);
	return 0;
}

static VkSurfaceKHR surface_khr;

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
	.toplevel = NULL,
};

static VkSurfaceKHR surface_khr;

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

static uint8_t use_command_buffers(
	VkDevice device,
	VkCommandBuffer *command_buffers)
{
	VkSemaphore image_available_semaphore;
	VkSemaphore render_finished_semaphore;

	VkSemaphoreCreateInfo semaphore_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
	};
	VkResult result;
	result = vkCreateSemaphore(device, &semaphore_create_info, NULL, &image_available_semaphore);
	if (result != VK_SUCCESS) {
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	result = vkCreateSemaphore(device, &semaphore_create_info, NULL, &render_finished_semaphore);
	if (result != VK_SUCCESS) {
		vkDestroySemaphore(device, image_available_semaphore, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	uint32_t image_index;
	result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
	                               image_available_semaphore,
	                               VK_NULL_HANDLE, &image_index);
	if (result != VK_SUCCESS) {
		vkDestroySemaphore(device, render_finished_semaphore, NULL);
		vkDestroySemaphore(device, image_available_semaphore, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkSemaphore wait_semaphores[] = {
		image_available_semaphore,
	};
	VkPipelineStageFlags wait_stages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};
	VkSemaphore signal_semaphores[] = {
		render_finished_semaphore,
	};
	VkCommandBuffer submit_command_buffers[] = {
		command_buffers[image_index],
	};
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = NULL,
		.waitSemaphoreCount = ARRAY_SIZE(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = ARRAY_SIZE(submit_command_buffers),
		.pCommandBuffers = submit_command_buffers,
		.signalSemaphoreCount = ARRAY_SIZE(signal_semaphores),
		.pSignalSemaphores = signal_semaphores,
	};
	VkSubmitInfo submits[] = {
		submit_info,
	};
	result = vkQueueSubmit(queue, ARRAY_SIZE(submits), submits, VK_NULL_HANDLE);
	if (result != VK_SUCCESS) {
		vkDestroySemaphore(device, render_finished_semaphore, NULL);
		vkDestroySemaphore(device, image_available_semaphore, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkSwapchainKHR swapchains[] = {
		swapchain,
	};
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.waitSemaphoreCount = ARRAY_SIZE(signal_semaphores),
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = ARRAY_SIZE(swapchains),
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
		.pResults = NULL,
	};

	result = vkQueuePresentKHR(queue, &present_info);
	if (result != VK_SUCCESS) {
		vkDestroySemaphore(device, render_finished_semaphore, NULL);
		vkDestroySemaphore(device, image_available_semaphore, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	result = vkDeviceWaitIdle(device);
	if (result != VK_SUCCESS) {
		vkDestroySemaphore(device, render_finished_semaphore, NULL);
		vkDestroySemaphore(device, image_available_semaphore, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	vkDestroySemaphore(device, render_finished_semaphore, NULL);
	vkDestroySemaphore(device, image_available_semaphore, NULL);
	return 0;
}

static uint8_t use_framebuffers(
	VkDevice device,
	VkRenderPass render_pass,
	VkPipeline graphics_pipeline,
	VkFramebuffer *swapchain_framebuffers,
	uint32_t swapchain_framebuffer_count)
{
	VkCommandPoolCreateInfo command_pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		/* Assumption: there is only one queue family */
		.queueFamilyIndex = 0,
	};

	VkResult result;
	VkCommandPool command_pool;
	result = vkCreateCommandPool(device, &command_pool_create_info, NULL,
	                             &command_pool);
	if (result != VK_SUCCESS) {
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkCommandBuffer *command_buffers = malloc(
		swapchain_framebuffer_count * sizeof(VkCommandBuffer)
	);
	if (command_buffers == NULL) {
		vkDestroyCommandPool(device, command_pool, NULL);
		return LIBC_ERROR_BIT;
	}

	VkCommandBufferAllocateInfo command_buffer_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = swapchain_framebuffer_count,
	};

	result = vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
	                                  command_buffers);
	if (result != VK_SUCCESS) {
		free(command_buffers);
		vkDestroyCommandPool(device, command_pool, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	for (uint32_t i = 0; i < swapchain_framebuffer_count; ++i) {
		VkCommandBufferBeginInfo command_buffer_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = NULL,
			.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
			.pInheritanceInfo = NULL,
		};
		result = vkBeginCommandBuffer(command_buffers[i],
		                              &command_buffer_begin_info);
		if (result != VK_SUCCESS) {
			vkFreeCommandBuffers(device, command_pool,
			                     swapchain_framebuffer_count,
			                     command_buffers);
			free(command_buffers);
			vkDestroyCommandPool(device, command_pool, NULL);
			uint8_t ret = VULKAN_ERROR_BIT;
			ret |= print_result(result);
			return ret;
		}

		VkClearValue clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
		VkClearValue clear_values[] = {
			clear_value,
		};
		VkRenderPassBeginInfo render_pass_begin_info = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.pNext = NULL,
			.renderPass = render_pass,
			.framebuffer = swapchain_framebuffers[i],
			.renderArea = {
				.offset = {
					.x = 0,
					.y = 0,
				},
				.extent = swapchain_image_extent,
			},
			.clearValueCount = ARRAY_SIZE(clear_values),
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(command_buffers[i], &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
		vkCmdDraw(command_buffers[i], 3, 1, 0, 0);
		vkCmdEndRenderPass(command_buffers[i]);

		result = vkEndCommandBuffer(command_buffers[i]);
		if (result != VK_SUCCESS) {
			vkFreeCommandBuffers(device, command_pool,
			                     swapchain_framebuffer_count,
			                     command_buffers);
			free(command_buffers);
			vkDestroyCommandPool(device, command_pool, NULL);
			uint8_t ret = VULKAN_ERROR_BIT;
			ret |= print_result(result);
			return ret;
		}
	}

	uint8_t ret = use_command_buffers(device, command_buffers);

	vkFreeCommandBuffers(device, command_pool, swapchain_framebuffer_count,
	                     command_buffers);
	free(command_buffers);
	vkDestroyCommandPool(device, command_pool, NULL);
	return ret;
}

static uint8_t use_shader_modules(
	VkDevice device,
	VkImageView *image_views,
	uint32_t image_view_count,
	VkShaderModule frag_shader_module,
	VkShaderModule vert_shader_module)
{
	VkPipelineShaderStageCreateInfo pipeline_shader_frag_stage_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = frag_shader_module,
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkPipelineShaderStageCreateInfo pipeline_shader_vert_stage_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vert_shader_module,
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkPipelineShaderStageCreateInfo pipeline_shader_stages[] = {
		pipeline_shader_vert_stage_create_info,
		pipeline_shader_frag_stage_create_info,
	};

	VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = NULL,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = NULL,
	};

	VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float) WIDTH,
		.height = (float) HEIGHT,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	VkViewport viewports[] = { viewport };

	VkRect2D scissor = {
		.offset = {
			.x = 0,
			.y = 0,
		},
		.extent = swapchain_image_extent,
	};
	VkRect2D scissors[] = { scissor };

	VkPipelineViewportStateCreateInfo pipeline_viewport_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.viewportCount = ARRAY_SIZE(viewports),
		.pViewports = viewports,
		.scissorCount = ARRAY_SIZE(scissors),
		.pScissors = scissors,
	};

	VkPipelineRasterizationStateCreateInfo pipeline_rasterization_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = 0.0f,
	};

	VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 1.0f,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	VkPipelineColorBlendAttachmentState pipeline_color_blend_attachment_state = {
		.blendEnable = VK_FALSE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
		                  | VK_COLOR_COMPONENT_G_BIT
		                  | VK_COLOR_COMPONENT_B_BIT
		                  | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendAttachmentState attachments[] = {
		pipeline_color_blend_attachment_state,
	};

	VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = ARRAY_SIZE(attachments),
		.pAttachments = attachments,
		.blendConstants = {
			[0] = 0.0f,
			[1] = 0.0f,
			[2] = 0.0f,
			[3] = 0.0f,
		},
	};

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_LINE_WIDTH
	};
	VkPipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.dynamicStateCount = ARRAY_SIZE(dynamic_states),
		.pDynamicStates = dynamic_states,
	};

	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.setLayoutCount = 0,
		.pSetLayouts = NULL,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL,
	};

	VkResult result;
	VkPipelineLayout pipeline_layout;
	result = vkCreatePipelineLayout(device, &pipeline_layout_create_info,
	                                NULL, &pipeline_layout);
	if (result != VK_SUCCESS) {
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkAttachmentDescription color_attachment_description = {
		.flags = 0,
		.format = swapchain_image_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	VkAttachmentDescription color_attachment_descriptions[] = {
		color_attachment_description,
	};

	VkAttachmentReference color_attachment_reference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference color_attachments_references[] = {
		color_attachment_reference,
	};

	VkSubpassDescription subpass_description = {
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = NULL,
		.colorAttachmentCount = ARRAY_SIZE(color_attachments_references),
		.pColorAttachments = color_attachments_references,
		.pResolveAttachments = NULL,
		.pDepthStencilAttachment = NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL,
	};
	VkSubpassDescription subpass_descriptions[] = {
		subpass_description,
	};

	VkSubpassDependency subpass_dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
		                 | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = 0,
	};
	VkSubpassDependency dependencies[] = {
		subpass_dependency,
	};

	VkRenderPassCreateInfo render_pass_create_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = ARRAY_SIZE(color_attachment_descriptions),
		.pAttachments = color_attachment_descriptions,
		.subpassCount = ARRAY_SIZE(subpass_descriptions),
		.pSubpasses = subpass_descriptions,
		.dependencyCount = ARRAY_SIZE(dependencies),
		.pDependencies = dependencies,
	};

	VkRenderPass render_pass;
	result = vkCreateRenderPass(device, &render_pass_create_info, NULL,
	                            &render_pass);
	if (result != VK_SUCCESS) {
		vkDestroyPipelineLayout(device, pipeline_layout, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = &pipeline_vertex_input_state_create_info,
		.pInputAssemblyState = &pipeline_input_assembly_state_create_info,
		.pTessellationState = NULL,
		.pViewportState = &pipeline_viewport_state_create_info,
		.pRasterizationState = &pipeline_rasterization_state_create_info,
		.pMultisampleState = &pipeline_multisample_state_create_info,
		.pDepthStencilState = NULL,
		.pColorBlendState = &pipeline_color_blend_state_create_info,
		.pDynamicState = NULL,
		.layout = pipeline_layout,
		.renderPass = render_pass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};
	VkGraphicsPipelineCreateInfo graphics_pipeline_create_infos[1] = {
		graphics_pipeline_create_info,
	};

	VkPipeline graphics_pipelines[1];
	result = vkCreateGraphicsPipelines(
		device,
		VK_NULL_HANDLE,                  /* pipelineCache */
		ARRAY_SIZE(graphics_pipeline_create_infos),
		graphics_pipeline_create_infos,  /* pCreateInfos */
		NULL,                            /* pAllocator */
		graphics_pipelines               /* pPipelines */
	);
	if (result != VK_SUCCESS) {
		vkDestroyRenderPass(device, render_pass, NULL);
		vkDestroyPipelineLayout(device, pipeline_layout, NULL);
		uint8_t ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkFramebuffer *swapchain_framebuffers = malloc(
		image_view_count * sizeof(VkFramebuffer)
	);
	if (swapchain_framebuffers == NULL) {
		vkDestroyPipeline(device, graphics_pipelines[0], NULL);
		vkDestroyRenderPass(device, render_pass, NULL);
		vkDestroyPipelineLayout(device, pipeline_layout, NULL);
		return LIBC_ERROR_BIT;
	}

	for (uint32_t i = 0; i < image_view_count; ++i) {
		VkImageView attachments[] = {
			image_views[i],
		};
		VkFramebufferCreateInfo framebuffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.renderPass = render_pass,
			.attachmentCount = ARRAY_SIZE(attachments),
			.pAttachments = attachments,
			.width = swapchain_image_extent.width,
			.height = swapchain_image_extent.height,
			.layers = 1,
		};
		result = vkCreateFramebuffer(device,
		                             &framebuffer_create_info,
		                             NULL,
		                             &(swapchain_framebuffers[i]));
		if (result != VK_SUCCESS) {
			for (uint32_t j = 0; j < i; ++j) {
				vkDestroyFramebuffer(device,
				                     swapchain_framebuffers[j],
				                     NULL);
			}
			free(swapchain_framebuffers);
			vkDestroyPipeline(device, graphics_pipelines[0], NULL);
			vkDestroyRenderPass(device, render_pass, NULL);
			vkDestroyPipelineLayout(device, pipeline_layout, NULL);
			uint8_t ret = VULKAN_ERROR_BIT;
			ret |= print_result(result);
			return ret;
		}
	}

	uint8_t ret = use_framebuffers(device,
	                               render_pass,
	                               graphics_pipelines[0],
	                               swapchain_framebuffers,
	                               image_view_count);

	for (uint32_t i = 0; i < image_view_count; ++i) {
		vkDestroyFramebuffer(device, swapchain_framebuffers[i], NULL);
	}
	free(swapchain_framebuffers);
	vkDestroyPipeline(device, graphics_pipelines[0], NULL);
	vkDestroyRenderPass(device, render_pass, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);
	return ret;
}

uint8_t use_image_views(VkDevice device,
                        VkImageView *image_views,
                        uint32_t image_view_count)
{
	const uint32_t *frag_code;
	size_t frag_code_size;

	uint8_t ret = mmap_file("frag.spv", &frag_code, &frag_code_size);
	if (ret != 0) {
		return ret;
	}

	const uint32_t *vert_code;
	size_t vert_code_size;
	ret = mmap_file("vert.spv", &vert_code, &vert_code_size);
	if (ret != 0) {
		munmap((void *) frag_code, frag_code_size);
		return ret;
	}

	VkShaderModuleCreateInfo shader_module_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.codeSize = frag_code_size,
		.pCode = frag_code,
	};
	VkResult result;
	VkShaderModule frag_shader_module;
	result = vkCreateShaderModule(device, &shader_module_create_info, NULL,
	                              &frag_shader_module);
	if (result != VK_SUCCESS) {
		munmap((void *) vert_code, vert_code_size);
		munmap((void *) frag_code, frag_code_size);
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	shader_module_create_info.codeSize = vert_code_size;
	shader_module_create_info.pCode = vert_code;

	VkShaderModule vert_shader_module;
	result = vkCreateShaderModule(device, &shader_module_create_info, NULL,
	                              &vert_shader_module);
	if (result != VK_SUCCESS) {
		vkDestroyShaderModule(device, frag_shader_module, NULL);
		munmap((void *) vert_code, vert_code_size);
		munmap((void *) frag_code, frag_code_size);
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	ret = use_shader_modules(device, image_views, image_view_count,
	                         frag_shader_module, vert_shader_module);

	vkDestroyShaderModule(device, vert_shader_module, NULL);
	vkDestroyShaderModule(device, frag_shader_module, NULL);
	munmap((void *) vert_code, vert_code_size);
	munmap((void *) frag_code, frag_code_size);
	return 0;
}

uint8_t use_swapchain(VkDevice device, VkSwapchainKHR swapchain)
{
	uint32_t swapchain_image_count;

	VkResult result;
	result = vkGetSwapchainImagesKHR(device, swapchain,
	                                 &swapchain_image_count, NULL);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkImage *swapchain_images = malloc(
		swapchain_image_count * sizeof(VkImage)
	);
	if (swapchain_images == NULL) {
		return LIBC_ERROR_BIT;
	}

	result = vkGetSwapchainImagesKHR(device, swapchain,
	                                 &swapchain_image_count,
	                                 swapchain_images);
	if (result != VK_SUCCESS) {
		free(swapchain_images);
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkImageView *image_views = malloc(
		swapchain_image_count * sizeof(VkImageView)
	);
	if (image_views == NULL) {
		free(swapchain_images);
		return LIBC_ERROR_BIT;
	}

	for (uint32_t i = 0; i < swapchain_image_count; ++i) {
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.image = swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain_image_format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		VkResult result;
		result = vkCreateImageView(device, &image_view_create_info,
		                           NULL, &(image_views[i]));
		if (result != VK_SUCCESS) {
			for (uint32_t j = 0; j < i; ++j) {
				vkDestroyImageView(device, image_views[j], NULL);
			}
			free(image_views);
			free(swapchain_images);
			int ret = VULKAN_ERROR_BIT;
			ret |= print_result(result);
			return ret;
		}
	}

	int ret = use_image_views(device, image_views, swapchain_image_count);

	for (uint32_t i = 0; i < swapchain_image_count; ++i) {
		vkDestroyImageView(device, image_views[i], NULL);
	}
	free(image_views);
	free(swapchain_images);
	return ret;
}

uint8_t use_device(VkDevice device)
{
	/* Assumption: there is only one queue family */
	uint32_t queue_family_index = 0;
	uint32_t queue_index = 0;
	vkGetDeviceQueue(device, queue_family_index, queue_index, &queue);

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = NULL,
		.flags = 0,
		.surface = surface_khr,
		.minImageCount = min_image_count,
		.imageFormat = swapchain_image_format,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = swapchain_image_extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
		.preTransform = current_transform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
	};

	VkResult result = vkCreateSwapchainKHR(
		device,
		&swapchain_create_info,
		NULL,
		&swapchain
	);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	int ret = use_swapchain(device, swapchain);

	vkDestroySwapchainKHR(device, swapchain, NULL);
	return ret;
}

uint8_t physical_device_capabilities(VkPhysicalDevice physical_device)
{
	VkSurfaceCapabilitiesKHR surface_capabilities_khr;
	VkResult result;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physical_device,
		surface_khr,
		&surface_capabilities_khr
	);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	if (WIDTH > surface_capabilities_khr.maxImageExtent.width ||
			WIDTH < surface_capabilities_khr.minImageExtent.width) {
		return APP_ERROR_BIT;
	}

	if (HEIGHT > surface_capabilities_khr.maxImageExtent.height ||
			HEIGHT < surface_capabilities_khr.minImageExtent.height) {
		return APP_ERROR_BIT;
	}

	min_image_count = surface_capabilities_khr.minImageCount;
	current_transform = surface_capabilities_khr.currentTransform;

	return 0;
}

uint8_t physical_device_has_swapchain_extension(
	VkPhysicalDevice physical_device,
	bool *has_swapchain_extension)
{
	*has_swapchain_extension = false;

	VkResult result;
	uint32_t extension_property_count;
	result = vkEnumerateDeviceExtensionProperties(physical_device,
	                                              NULL,
	                                              &extension_property_count,
	                                              NULL);
	if (result != VK_SUCCESS) {
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	VkExtensionProperties *extension_properties = malloc(
		extension_property_count * sizeof(VkExtensionProperties)
	);
	if (extension_properties == NULL) {
		return LIBC_ERROR_BIT;
	}
	result = vkEnumerateDeviceExtensionProperties(physical_device,
	                                              NULL,
	                                              &extension_property_count,
	                                              extension_properties);
	if (result != VK_SUCCESS) {
		free(extension_properties);
		int ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	for (uint32_t i = 0; i < extension_property_count; ++i) {
		if (strcmp(extension_properties[i].extensionName, "VK_KHR_swapchain") == 0) {
			*has_swapchain_extension = true;
		}
	}
	free(extension_properties);

	return 0;
}

uint8_t use_physical_device(VkPhysicalDevice physical_device)
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

	const float queue_priorities[] = {1.0f};
	VkDeviceQueueCreateInfo device_queue_create_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			/* Assumption: there is only one queue family */
			.queueFamilyIndex = 0,
			.queueCount = ARRAY_SIZE(queue_priorities),
			.pQueuePriorities = queue_priorities,
		},
	};

	bool has_swapchain_extension;
	int ret = physical_device_has_swapchain_extension(
		physical_device,
		&has_swapchain_extension
	);
	if (ret != 0) {
		return ret;
	}
	if (!has_swapchain_extension) {
		/* Graphics card cant present image directly to screen */
		return APP_ERROR_BIT;
	}

	ret = physical_device_capabilities(physical_device);
	if (ret != 0) {
		return ret;
	}

	const char *const enabled_extension_names[] = {
		"VK_KHR_swapchain",
	};
	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.queueCreateInfoCount = ARRAY_SIZE(device_queue_create_infos),
		.pQueueCreateInfos = device_queue_create_infos,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = ARRAY_SIZE(enabled_extension_names),
		.ppEnabledExtensionNames = enabled_extension_names,
		.pEnabledFeatures = NULL,
	};
	VkDevice device;
	VkResult result;
	result = vkCreateDevice(physical_device,
	                        &device_create_info,
	                        NULL,
	                        &device);
	if (result != VK_SUCCESS) {
		ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	ret = use_device(device);

	vkDestroyDevice(device, NULL);
	return ret;
}

uint8_t use_physical_devices(VkPhysicalDevice *physical_devices,
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

uint8_t use_instance(VkInstance instance)
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

	int ret = use_physical_devices(physical_devices, physical_device_count);

	vkDestroySurfaceKHR(instance, surface_khr, NULL);
	free(physical_devices);
	return ret;
}

static void wl_registry_global(void *data,
                               struct wl_registry *wl_registry,
                               uint32_t name,
                               const char *interface,
                               uint32_t version)
{
	(void) data;

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
	                                    0, 0, WIDTH, HEIGHT);
	zxdg_toplevel_v6_set_title(wayland.toplevel, "Hello Vulkan");

	return 0;
}

static void wayland_fini()
{
	if (wayland.toplevel != NULL) {
		zxdg_toplevel_v6_destroy(wayland.toplevel);
		wayland.toplevel = NULL;
	}
	if (wayland.shell_surface != NULL) {
		zxdg_surface_v6_destroy(wayland.shell_surface);
		wayland.shell_surface = NULL;
	}
	if (wayland.surface != NULL) {
		wl_surface_destroy(wayland.surface);
		wayland.surface = NULL;
	}
	if (wayland.shell != NULL) {
		zxdg_shell_v6_destroy(wayland.shell);
		wayland.shell = NULL;
	}
	if (wayland.compositor != NULL) {
		wl_compositor_destroy(wayland.compositor);
		wayland.compositor = NULL;
	}
	if (wayland.registry != NULL) {
		wl_registry_destroy(wayland.registry);
		wayland.registry = NULL;
	}
	if (wayland.display != NULL) {
		wl_display_disconnect(wayland.display);
		wayland.display = NULL;
	}
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	int ret = wayland_init();
	if (ret != 0) {
		return ret;
	}

	const char *enabled_layer_names[] = {
		"VK_LAYER_LUNARG_standard_validation",
	};
	const char *enabled_extension_names[] = {
		"VK_KHR_surface",
		"VK_KHR_wayland_surface",
	};
	VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.pApplicationInfo = NULL,
		.enabledLayerCount = ARRAY_SIZE(enabled_layer_names),
		.ppEnabledLayerNames = enabled_layer_names,
		.enabledExtensionCount = ARRAY_SIZE(enabled_extension_names),
		.ppEnabledExtensionNames = enabled_extension_names,
	};
	VkInstance instance;
	VkResult result;
	result = vkCreateInstance(&instance_create_info, NULL, &instance);
	if (result != VK_SUCCESS) {
		wayland_fini();
		ret = VULKAN_ERROR_BIT;
		ret |= print_result(result);
		return ret;
	}

	ret = use_instance(instance);

	vkDestroyInstance(instance, NULL);
	wayland_fini();
	return ret;
}
