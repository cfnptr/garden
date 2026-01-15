// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/***********************************************************************************************************************
 * @file
 * @brief Vulkan API graphics swapchain functions.
 */

#pragma once
#include "garden/graphics/swapchain.hpp"
#include "garden/graphics/framebuffer.hpp"
#include "garden/graphics/vulkan/vma.hpp"

namespace garden
{
	class VulkanAPI;
}

namespace garden::graphics
{

/**
 * @brief Vulkan API swapchain class.
 * @warning Use Vulkan swapchain directly with caution!
 */
class VulkanSwapchain final : public Swapchain
{
public:
	/**
	 * @brief Vulkan swapchain in0flight frame data container.
	 */
	struct InFlightFrame
	{
		vector<vk::CommandPool> secondaryCommandPools;
		vector<vk::CommandBuffer> secondaryCommandBuffers;
		vk::Fence fence;
		vk::Semaphore imageAvailableSemaphore;
		vk::CommandBuffer primaryCommandBuffer;
		uint32 secondaryCommandBufferIndex = 0;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		vk::QueryPool queryPool;
		bool isPoolClean = false;
		#endif
	};
private:
	uint16 _alignment = 0;
	VulkanAPI* vulkanAPI = nullptr;
	InFlightFrame inFlightFrames[inFlightCount];
	vector<vk::Semaphore> renderFinishedSemaphores;
	vector<vk::Format> colorAttachmentFormats;
	vector<vk::CommandBuffer> secondaryCommandBuffers;
	vk::SwapchainKHR instance = {};

	VulkanSwapchain(VulkanAPI* vulkanAPI, uint2 framebufferSize,
		bool useVsync, bool useTripleBuffering);
	~VulkanSwapchain() final;

	friend class garden::VulkanAPI;
public:
	vk::SwapchainKHR getInstance() noexcept { return instance; }
	InFlightFrame& getInFlightFrame() noexcept { return inFlightFrames[inFlightIndex]; }

	void recreate(uint2 framebufferSize, bool useVsync, bool useTripleBuffering) final;
	bool acquireNextImage() final;
	void submit() final;
	bool present() final;

	void beginSecondaryCommandBuffers(vk::Framebuffer framebuffer, vk::RenderPass renderPass, 
		uint8 subpassIndex, const vector<Framebuffer::OutputAttachment>& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment, const string& name);
	void endSecondaryCommandBuffers();
};

} // namespace garden::graphics