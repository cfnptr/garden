// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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
	 * @brief Vulkan swapchain buffer data container.
	 */
	struct VkBuffer final : public Swapchain::Buffer
	{
		uint32 secondaryCommandBufferIndex = 0;
		vk::CommandBuffer primaryCommandBuffer;
		vector<vk::CommandPool> secondaryCommandPools;
		vector<vk::CommandBuffer> secondaryCommandBuffers;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		vk::QueryPool queryPool;
		bool isPoolClean = false;
		#endif
	};
private:
	uint16 _alignment = 0;
	VulkanAPI* vulkanAPI = nullptr;
	vector<VkBuffer*> vulkanBuffers;
	vector<vk::Format> colorAttachmentFormats;
	vector<vk::CommandBuffer> secondaryCommandBuffers;
	vk::Fence fences[frameLag];
	vk::Semaphore imageAcquiredSemaphores[frameLag];
	vk::Semaphore drawCompleteSemaphores[frameLag];
	vk::SwapchainKHR instance = {};
	uint32 frameIndex = 0;

	VulkanSwapchain(VulkanAPI* vulkanAPI, uint2 framebufferSize,
		bool useVsync, bool useTripleBuffering);
	~VulkanSwapchain() final;

	friend class garden::VulkanAPI;
public:
	VkBuffer* getCurrentVkBuffer() const noexcept { return vulkanBuffers[bufferIndex]; }

	void recreate(uint2 framebufferSize, bool useVsync, bool useTripleBuffering) final;
	bool acquireNextImage(ThreadPool* threadPool) final;
	void submit() final;
	bool present() final;

	void beginSecondaryCommandBuffers(vk::Framebuffer framebuffer, vk::RenderPass renderPass, 
		uint8 subpassIndex, const vector<Framebuffer::OutputAttachment>& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment, const string& name);
	void endSecondaryCommandBuffers();
};

} // namespace garden::graphics