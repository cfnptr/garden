// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

// TODO: add documentation and make generic Swapchain class.

#pragma once
#include "garden/thread-pool.hpp"
#include "garden/graphics/framebuffer.hpp"

#if __APPLE__
#define VK_ENABLE_BETA_EXTENSIONS
#endif

#include "vulkan/vulkan.hpp"
#include "vk_mem_alloc.h"

namespace garden::graphics
{

class Swapchain final
{
public:
	struct Buffer final
	{
		vk::CommandBuffer primaryCommandBuffer = {};
		ID<Image> colorImage = {};
		uint32 secondaryCommandBufferIndex = 0;
		vector<vk::CommandPool> secondaryCommandPools;
		vector<vk::CommandBuffer> secondaryCommandBuffers;
		#if GARDEN_DEBUG || GARDEN_EDITOR
		vk::QueryPool queryPool = {};
		uint32 isPoolClean = false;
		#endif
	};
private:
	uint2 framebufferSize = uint2(0);
	vk::Fence fences[frameLag];
	vk::Semaphore imageAcquiredSemaphores[frameLag];
	vk::Semaphore drawCompleteSemaphores[frameLag];
	vk::SwapchainKHR instance = {};
	vector<Buffer> buffers;
	ThreadPool* threadPool = nullptr;
	uint32 frameIndex = 0, bufferIndex = 0;
	bool vsync = false, tripleBuffering = false, useThreading = false;

	Swapchain() = default;
	Swapchain(uint2 framebufferSize, bool useVsync,
		bool useTripleBuffering, bool useThreading);
	void destroy();

	friend class Vulkan;
public:
	const vector<Buffer>& getBuffers() const noexcept { return buffers; }
	uint32 getBufferCount() const noexcept { return (uint32)buffers.size(); }
	const Buffer& getCurrentBuffer() const noexcept { return buffers[bufferIndex]; }
	uint32 getCurrentBufferIndex() const noexcept { return bufferIndex; }
	uint32 getCurrentFrameIndex() const noexcept { return frameIndex; }
	uint2 getFramebufferSize() const noexcept { return framebufferSize; }
	bool useVsync() const noexcept { return vsync; }
	bool useTripleBuffering() const noexcept { return tripleBuffering; }

	void setThreadPool(ThreadPool& threadPool);
	void recreate(uint2 framebufferSize, bool useVsync, bool useTripleBuffering);
	bool acquireNextImage();
	void submit();
	bool present();

	void beginSecondaryCommandBuffers(void* framebuffer, void* renderPass,
		uint8 subpassIndex, const vector<Framebuffer::OutputAttachment>& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment, const string& name);
	void endSecondaryCommandBuffers();
};

} // namespace garden::graphics