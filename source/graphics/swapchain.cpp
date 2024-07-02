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

#include "garden/graphics/swapchain.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace garden::graphics;

static uint32 getBestVkImageCount(const vk::SurfaceCapabilitiesKHR& capabilities, bool useTripleBuffering)
{
	auto imageCount = capabilities.minImageCount;
	auto maxImageCount = capabilities.maxImageCount;
	if (useTripleBuffering && imageCount < 3)
		imageCount = 3;
	if (maxImageCount > 0 && imageCount > maxImageCount)
		imageCount = maxImageCount;
	return imageCount;
}

//*********************************************************************************************************************
static vk::SurfaceFormatKHR getBestVkSurfaceFormat(
	vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool useHDR)
{
	auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
	if (formats.empty())
		throw runtime_error("No suitable surface format.");

	if (useHDR)
	{
		for (auto format : formats)
		{
			if (format.format == vk::Format::eA2R10G10B10UnormPack32 &&
				format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			{
				return format;
			}
		}
	}
	
	for (auto format : formats)
	{
		if (format.format == vk::Format::eB8G8R8A8Srgb &&
			format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			return format;
		}
	}
	for (auto format : formats)
	{
		if (format.format == vk::Format::eR8G8B8A8Srgb &&
			format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			return format;
		}
	}

	return formats[0];
}

//*********************************************************************************************************************
static vk::Extent2D getBestVkSurfaceExtent(const vk::SurfaceCapabilitiesKHR& capabilities, int2 framebufferSize)
{
	if (capabilities.currentExtent.width == UINT32_MAX)
	{
		return vk::Extent2D(
			clamp((uint32)framebufferSize.x,
				capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			clamp((uint32)framebufferSize.y,
				capabilities.minImageExtent.height, capabilities.maxImageExtent.height));
	}
	return capabilities.currentExtent;
}

static vk::SurfaceTransformFlagBitsKHR getBestVkSurfaceTransform(const vk::SurfaceCapabilitiesKHR& capabilities)
{
	if (capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
		return vk::SurfaceTransformFlagBitsKHR::eIdentity;
	return capabilities.currentTransform;
}

static vk::CompositeAlphaFlagBitsKHR getBestVkCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities)
{
	auto supportedCompositeAlpha = capabilities.supportedCompositeAlpha;
	if (supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)
		return vk::CompositeAlphaFlagBitsKHR::eOpaque;
	if (supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
		return vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
	if (supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
		return vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
	if (supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
		return vk::CompositeAlphaFlagBitsKHR::eInherit;
	throw runtime_error("No suitable composite alpha.");
}

static vk::PresentModeKHR getBestVkPresentMode(
	vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool useVsync)
{
	auto modes = physicalDevice.getSurfacePresentModesKHR(surface);
	if (modes.empty())
		throw runtime_error("No suitable present mode.");

	if (useVsync)
		return vk::PresentModeKHR::eFifo;

	for (auto mode : modes)
	{
		if (mode == vk::PresentModeKHR::eMailbox)
			return vk::PresentModeKHR::eMailbox;
	}
	for (auto mode : modes)
	{
		if (mode == vk::PresentModeKHR::eImmediate)
			return vk::PresentModeKHR::eImmediate;
	}
	for (auto mode : modes)
	{
		if (mode == vk::PresentModeKHR::eFifoRelaxed)
			return vk::PresentModeKHR::eFifoRelaxed;
	}

	return vk::PresentModeKHR::eFifo;
}

//*********************************************************************************************************************
static vk::SwapchainKHR createVkSwapchain(vk::PhysicalDevice physicalDevice,
	vk::Device device, vk::SurfaceKHR surface, int2 framebufferSize, bool useVsync,
	bool useTripleBuffering, vk::SwapchainKHR oldSwapchain, vk::Format& format)
{
	auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
	auto imageCount = getBestVkImageCount(capabilities, useTripleBuffering);
	auto surfaceFormat = getBestVkSurfaceFormat(physicalDevice, surface, false);
	auto surfaceExtent = getBestVkSurfaceExtent(capabilities, framebufferSize);
	auto surfaceTransform = getBestVkSurfaceTransform(capabilities);
	auto compositeAlpha = getBestVkCompositeAlpha(capabilities);
	auto presentMode = getBestVkPresentMode(physicalDevice, surface, useVsync);
	format = surfaceFormat.format;

	vk::SwapchainCreateInfoKHR swapchainInfo({}, surface, imageCount, surfaceFormat.format,
		surfaceFormat.colorSpace, surfaceExtent, 1, vk::ImageUsageFlagBits::eColorAttachment | 
		vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, {}, 
		surfaceTransform, compositeAlpha, presentMode, VK_TRUE, oldSwapchain);
	return device.createSwapchainKHR(swapchainInfo);
}

static vk::Fence createVkFence(vk::Device device, bool isSignaled = false)
{
	vk::FenceCreateInfo fenceInfo(
		isSignaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags());
	return device.createFence(fenceInfo);
}
static vk::Semaphore createVkSemaphore(vk::Device device)
{
	vk::SemaphoreCreateInfo semaphoreInfo;
	return device.createSemaphore(semaphoreInfo);
}

static vector<vk::CommandPool> createVkCommandPools(vk::Device device,
	uint32 queueFamilyIndex, uint32 count, bool isTransient = false)
{
	vector<vk::CommandPool> commandPools(count);
	vk::CommandPoolCreateFlags flags;
	if (isTransient)
		flags |= vk::CommandPoolCreateFlagBits::eTransient;
	vk::CommandPoolCreateInfo commandPoolInfo({}, queueFamilyIndex);
	for (uint32 i = 0; i < count; i++)
		commandPools[i] = device.createCommandPool(commandPoolInfo);
	return commandPools;
}

//*********************************************************************************************************************
static vector<Swapchain::Buffer> createVkSwapchainBuffers(
	vk::Device device, vk::SwapchainKHR swapchain, vk::Format surfaceFormat,
	vk::CommandPool graphicsCommandPool, LinearPool<Image>& imagePool,
	uint32 graphicsQueueFamilyIndex, int2 framebufferSize, bool useThreading)
{
	auto images = device.getSwapchainImagesKHR(swapchain);
	auto imageFormat = toImageFormat(surfaceFormat);
	const auto imageBind = Image::Bind::ColorAttachment | Image::Bind::TransferDst;
	vector<Swapchain::Buffer> buffers(images.size());

	vk::CommandBufferAllocateInfo commandBufferInfo(
		graphicsCommandPool, vk::CommandBufferLevel::ePrimary, 1);
	auto commandPoolCount = useThreading ? thread::hardware_concurrency() : 1;

	for (uint32 i = 0; i < (uint32)buffers.size(); i++)
	{
		auto& buffer = buffers[i];
		auto allocateResult = device.allocateCommandBuffers(
			&commandBufferInfo, &buffer.primaryCommandBuffer);
		vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
		
		#if GARDEN_DEBUG
		if (Vulkan::hasDebugUtils)
		{
			auto name = "commandBuffer.graphics.swapchain" + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eCommandBuffer,
				(uint64)(VkCommandBuffer)buffer.primaryCommandBuffer, name.c_str());
			device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
		#endif

		buffer.colorImage = imagePool.create((VkImage)images[i], imageFormat,
			imageBind, Image::Strategy::Default, framebufferSize, 0);
		buffer.secondaryCommandPools = createVkCommandPools(
			device, graphicsQueueFamilyIndex, commandPoolCount);

		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto colorImage = GraphicsAPI::imagePool.get(buffer.colorImage);
		colorImage->setDebugName("image.swapchain" + to_string(i));	
		#endif

		#if GARDEN_EDITOR
		vk::QueryPoolCreateInfo queryPoolInfo({}, vk::QueryType::eTimestamp, 2);
		buffer.queryPool = Vulkan::device.createQueryPool(queryPoolInfo);
		#endif
	}

	return buffers;
}
static void destroyVkSwapchainBuffers(vk::Device device, LinearPool<Image>& imagePool, 
	LinearPool<ImageView>& imageViewPool, const vector<Swapchain::Buffer>& buffers)
{
	for (const auto& buffer : buffers)
	{
		#if GARDEN_DEBUG || GARDEN_EDITOR
		device.destroyQueryPool(buffer.queryPool);
		#endif
		for (auto commandPool : buffer.secondaryCommandPools)
			device.destroyCommandPool(commandPool);
		auto imageView = imagePool.get(buffer.colorImage);
		if (imageView->hasDefaultView())
			imageViewPool.destroy(imageView->getDefaultView());
		imagePool.destroy(buffer.colorImage);
	}
}

//*********************************************************************************************************************
Swapchain::Swapchain(int2 framebufferSize, bool useVsync, bool useTripleBuffering, bool useThreading)
{
	for (uint8 i = 0; i < frameLag; i++)
	{
		fences[i] = createVkFence(Vulkan::device, true);
		imageAcquiredSemaphores[i] = createVkSemaphore(Vulkan::device);
		drawCompleteSemaphores[i] = createVkSemaphore(Vulkan::device);
	}

	vk::Format format;

	instance = createVkSwapchain(Vulkan::physicalDevice, Vulkan::device,
		Vulkan::surface, framebufferSize, useVsync, useTripleBuffering, nullptr, format);
	buffers = createVkSwapchainBuffers(Vulkan::device,
		instance, format, Vulkan::frameCommandPool, GraphicsAPI::imagePool,
		Vulkan::graphicsQueueFamilyIndex, framebufferSize, useThreading);

	this->framebufferSize = framebufferSize;
	this->vsync = useVsync;
	this->tripleBuffering = useTripleBuffering;
	this->useThreading = useThreading;
}
void Swapchain::destroy()
{
	destroyVkSwapchainBuffers(Vulkan::device, GraphicsAPI::imagePool, GraphicsAPI::imageViewPool, buffers);
	Vulkan::device.destroySwapchainKHR(instance);

	for (uint8 i = 0; i < frameLag; i++)
	{
		Vulkan::device.destroySemaphore(drawCompleteSemaphores[i]);
		Vulkan::device.destroySemaphore(imageAcquiredSemaphores[i]);
		Vulkan::device.destroyFence(fences[i]);
	}
}

void Swapchain::setThreadPool(ThreadPool& threadPool)
{
	this->threadPool = &threadPool;
}

//*********************************************************************************************************************
void Swapchain::recreate(int2 framebufferSize, bool useVsync, bool useTripleBuffering)
{
	Vulkan::device.waitIdle();
	vk::Format format;

	destroyVkSwapchainBuffers(Vulkan::device, GraphicsAPI::imagePool, GraphicsAPI::imageViewPool, buffers);
	auto newInstance = createVkSwapchain(Vulkan::physicalDevice, Vulkan::device,
		Vulkan::surface, framebufferSize, useVsync, useTripleBuffering, instance, format);
	Vulkan::device.destroySwapchainKHR(instance);

	buffers = createVkSwapchainBuffers(Vulkan::device,
		newInstance, format, Vulkan::frameCommandPool, GraphicsAPI::imagePool,
		Vulkan::graphicsQueueFamilyIndex, framebufferSize, useThreading);

	this->framebufferSize = framebufferSize;
	this->vsync = useVsync;
	this->tripleBuffering = useTripleBuffering;
	this->instance = newInstance;
	this->frameIndex = 0;
}

//*********************************************************************************************************************
bool Swapchain::acquireNextImage()
{
	auto fence = fences[frameIndex]; 
	auto waitResult = Vulkan::device.waitForFences(1, &fence, VK_FALSE, 10000000000); // Note: emergency 10 seconds timeout.
	vk::detail::resultCheck(waitResult, "vk::Device::waitForFences");
	Vulkan::device.resetFences(fence);

	auto result = Vulkan::device.acquireNextImageKHR(instance, UINT64_MAX,
		imageAcquiredSemaphores[frameIndex], VK_NULL_HANDLE, &bufferIndex);
		
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		return false;
	else if (result != vk::Result::eSuccess)
		throw runtime_error("Failed to acquire next image. (error: " + vk::to_string(result) + ")");

	auto& buffer = buffers[bufferIndex];
	if (useThreading)
	{
		threadPool->addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			Vulkan::device.resetCommandPool(buffer.secondaryCommandPools[task.getTaskIndex()]);
		}),
		(uint32)buffer.secondaryCommandPools.size());
		threadPool->wait();
	}
	else
	{
		Vulkan::device.resetCommandPool(buffer.secondaryCommandPools[0]);
	}

	buffer.secondaryCommandBufferIndex = 0;
	vmaSetCurrentFrameIndex(Vulkan::memoryAllocator, bufferIndex);
	return true;
}

//*********************************************************************************************************************
void Swapchain::submit()
{
	const auto& buffer = buffers[bufferIndex];
	vk::PipelineStageFlags pipelineStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	vk::SubmitInfo submitInfo(1, &imageAcquiredSemaphores[frameIndex], &pipelineStage, 
		1, &buffer.primaryCommandBuffer, 1, &drawCompleteSemaphores[frameIndex]);
	Vulkan::frameQueue.submit(submitInfo, fences[frameIndex]);
}
bool Swapchain::present()
{
	vk::PresentInfoKHR presentInfo(1, &drawCompleteSemaphores[frameIndex], 1, &instance, &bufferIndex);
	auto result = Vulkan::frameQueue.presentKHR(&presentInfo); // & is required here.

	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		return false;
	else if (result != vk::Result::eSuccess)
		throw runtime_error("Failed to present image. (error: " + vk::to_string(result) + ")");

	frameIndex = (frameIndex + 1) % frameLag;
	return true;
}

//*********************************************************************************************************************
static vector<vk::Format> colorAttachmentFormats;

void Swapchain::beginSecondaryCommandBuffers(void* framebuffer, void* renderPass, uint8 subpassIndex,
	const vector<Framebuffer::OutputAttachment>& colorAttachments,
	Framebuffer::OutputAttachment depthStencilAttachment, const string& name)
{
	auto& buffer = buffers[bufferIndex];
	const auto& secondaryCommandPools = buffer.secondaryCommandPools;
	auto commandBufferCount = (uint32)secondaryCommandPools.size();
	Vulkan::secondaryCommandBuffers.resize(commandBufferCount);
	Vulkan::secondaryCommandStates.resize(commandBufferCount);
	
	if (buffer.secondaryCommandBufferIndex < buffer.secondaryCommandBuffers.size())
	{
		memcpy(Vulkan::secondaryCommandBuffers.data(),
			buffer.secondaryCommandBuffers.data() + buffer.secondaryCommandBufferIndex,
			commandBufferCount * sizeof(vk::CommandBuffer));
	}
	else
	{
		buffer.secondaryCommandBuffers.resize(buffer.secondaryCommandBufferIndex + commandBufferCount);
		auto secondaryCommandBuffers = buffer.secondaryCommandBuffers.data() + buffer.secondaryCommandBufferIndex;
		vk::CommandBufferAllocateInfo allocateInfo(VK_NULL_HANDLE, vk::CommandBufferLevel::eSecondary, 1);
		
		for (uint32 i = 0; i < commandBufferCount; i++)
		{
			allocateInfo.commandPool = secondaryCommandPools[i];
			vk::CommandBuffer commandBuffer;
			auto allocateResult = Vulkan::device.allocateCommandBuffers(&allocateInfo, &commandBuffer);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
			secondaryCommandBuffers[i] = commandBuffer;
			Vulkan::secondaryCommandBuffers[i] = commandBuffer;

			#if GARDEN_DEBUG
			if (Vulkan::hasDebugUtils)
			{
				auto objectName = name + ".secondaryCommandBuffer" + to_string(i);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eCommandBuffer,
					(uint64)(VkCommandBuffer)commandBuffer, objectName.c_str());
				Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
			}
			#endif
		}
	}

	buffer.secondaryCommandBufferIndex += commandBufferCount;

	for (uint32 i = 0; i < commandBufferCount; i++)
		Vulkan::secondaryCommandStates[i] = false;

	vk::CommandBufferInheritanceInfo inheritanceInfo(
		(VkRenderPass)renderPass, subpassIndex, (VkFramebuffer)framebuffer, VK_FALSE); // TODO: occlusion query
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
		vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);

	vk::CommandBufferInheritanceRenderingInfoKHR inheritanceRenderingInfo;
	if (!renderPass)
	{
		if (colorAttachmentFormats.size() < colorAttachments.size())
			colorAttachmentFormats.resize(colorAttachments.size());

		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			auto imageView = GraphicsAPI::imageViewPool.get(colorAttachments[i].imageView);
			colorAttachmentFormats[i] = toVkFormat(imageView->getFormat());
		}

		if (depthStencilAttachment.imageView)
		{
			auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
			auto format = imageView->getFormat();

			if (isFormatDepthOnly(format))
			{
				inheritanceRenderingInfo.depthAttachmentFormat = toVkFormat(format);
				inheritanceRenderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;
			}
			else if (isFormatStencilOnly(format))
			{
				inheritanceRenderingInfo.depthAttachmentFormat = vk::Format::eUndefined;
				inheritanceRenderingInfo.stencilAttachmentFormat = toVkFormat(format);
			}
			else
			{
				inheritanceRenderingInfo.depthAttachmentFormat = inheritanceRenderingInfo.stencilAttachmentFormat =
					toVkFormat(imageView->getFormat());
			}
		}
		
		inheritanceRenderingInfo.viewMask = 0;
		inheritanceRenderingInfo.colorAttachmentCount = (uint32)colorAttachments.size();
		inheritanceRenderingInfo.pColorAttachmentFormats = colorAttachmentFormats.data();
		inheritanceRenderingInfo.rasterizationSamples = vk::SampleCountFlagBits::e1;
		inheritanceInfo.pNext = &inheritanceRenderingInfo;
	}

	threadPool->addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
	{
		Vulkan::secondaryCommandBuffers[task.getTaskIndex()].begin(beginInfo);
	}),
	(uint32)Vulkan::secondaryCommandBuffers.size());
	threadPool->wait();
}

//*********************************************************************************************************************
static vector<vk::CommandBuffer> secondaryCommandBuffers;

void Swapchain::endSecondaryCommandBuffers()
{
	threadPool->addTasks(ThreadPool::Task([](const ThreadPool::Task& task)
	{
		Vulkan::secondaryCommandBuffers[task.getTaskIndex()].end();
	}),
	(uint32)Vulkan::secondaryCommandBuffers.size());
	threadPool->wait();

	for (uint16 i = 0; i < (uint16)Vulkan::secondaryCommandStates.size(); i++)
	{
		if (Vulkan::secondaryCommandStates[i])
			secondaryCommandBuffers.push_back(Vulkan::secondaryCommandBuffers[i]);
	}

	if (!secondaryCommandBuffers.empty())
	{
		ExecuteCommand command;
		command.bufferCount = (uint16)secondaryCommandBuffers.size();
		command.buffers = secondaryCommandBuffers.data();
		GraphicsAPI::currentCommandBuffer->addCommand(command);
		secondaryCommandBuffers.clear();
	}

	Vulkan::secondaryCommandBuffers.clear();
}