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

#include "garden/graphics/vulkan/swapchain.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/profiler.hpp"

using namespace math;
using namespace garden;
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

//**********************************************************************************************************************
static vk::SurfaceFormatKHR getBestVkSurfaceFormat(
	vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool useHDR)
{
	auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
	if (formats.empty())
		throw GardenError("No suitable surface format.");

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

//**********************************************************************************************************************
static vk::Extent2D getBestVkSurfaceExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint2 framebufferSize)
{
	if (capabilities.currentExtent.width == UINT32_MAX)
	{
		return vk::Extent2D(
			clamp(framebufferSize.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			clamp(framebufferSize.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height));
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
	throw GardenError("No suitable composite alpha.");
}

static vk::PresentModeKHR getBestVkPresentMode(
	vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool useVsync)
{
	auto modes = physicalDevice.getSurfacePresentModesKHR(surface);
	if (modes.empty())
		throw GardenError("No suitable present mode.");

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

//**********************************************************************************************************************
static vk::SwapchainKHR createVkSwapchain(VulkanAPI* vulkanAPI, uint2 framebufferSize,
	bool useVsync, bool useTripleBuffering, vk::SwapchainKHR oldSwapchain, vk::Format& format)
{
	auto capabilities = vulkanAPI->physicalDevice.getSurfaceCapabilitiesKHR(vulkanAPI->surface);
	auto imageCount = getBestVkImageCount(capabilities, useTripleBuffering);
	auto surfaceFormat = getBestVkSurfaceFormat(vulkanAPI->physicalDevice, vulkanAPI->surface, false);
	auto surfaceExtent = getBestVkSurfaceExtent(capabilities, framebufferSize);
	auto surfaceTransform = getBestVkSurfaceTransform(capabilities);
	auto compositeAlpha = getBestVkCompositeAlpha(capabilities);
	auto presentMode = getBestVkPresentMode(vulkanAPI->physicalDevice, vulkanAPI->surface, useVsync);
	format = surfaceFormat.format;

	vk::SwapchainCreateInfoKHR swapchainInfo({}, vulkanAPI->surface, imageCount, surfaceFormat.format,
		surfaceFormat.colorSpace, surfaceExtent, 1, vk::ImageUsageFlagBits::eColorAttachment | 
		vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, {}, 
		surfaceTransform, compositeAlpha, presentMode, VK_TRUE, oldSwapchain);
	return vulkanAPI->device.createSwapchainKHR(swapchainInfo);
}

static vector<vk::CommandPool> createVkCommandPools(vk::Device device, uint32 queueFamilyIndex, uint32 count)
{
	vector<vk::CommandPool> commandPools(count);
	vk::CommandPoolCreateFlags flags;
	vk::CommandPoolCreateInfo commandPoolInfo({}, queueFamilyIndex);
	for (uint32 i = 0; i < count; i++)
		commandPools[i] = device.createCommandPool(commandPoolInfo);
	return commandPools;
}

//**********************************************************************************************************************
static vector<Swapchain::Buffer*> createVkSwapchainBuffers(VulkanAPI* vulkanAPI,
	vk::SwapchainKHR swapchain, uint2 framebufferSize, vk::Format surfaceFormat)
{
	auto images = vulkanAPI->device.getSwapchainImagesKHR(swapchain);
	auto imageFormat = toImageFormat(surfaceFormat);
	const auto imageBind = Image::Bind::ColorAttachment | Image::Bind::TransferDst;
	vector<Swapchain::Buffer*> buffers(images.size());
	vk::CommandBufferAllocateInfo commandBufferInfo(vulkanAPI->graphicsCommandPool, vk::CommandBufferLevel::ePrimary, 1);

	for (uint32 i = 0; i < (uint32)buffers.size(); i++)
	{
		auto buffer = new VulkanSwapchain::VkBuffer();
		auto allocateResult = vulkanAPI->device.allocateCommandBuffers(&commandBufferInfo, &buffer->primaryCommandBuffer);
		vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
		
		#if GARDEN_DEBUG
		if (vulkanAPI->hasDebugUtils)
		{
			auto name = "commandBuffer.graphics.swapchain" + to_string(i);
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eCommandBuffer,
				(uint64)(VkCommandBuffer)buffer->primaryCommandBuffer, name.c_str());
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
		}
		#endif

		buffer->colorImage = vulkanAPI->imagePool.create((VkImage)images[i], imageFormat,
			imageBind, Image::Strategy::Default, framebufferSize, 0);
		buffer->secondaryCommandPools = createVkCommandPools(vulkanAPI->device, 
			vulkanAPI->graphicsQueueFamilyIndex, vulkanAPI->threadCount);

		#if GARDEN_EDITOR
		vk::QueryPoolCreateInfo queryPoolInfo({}, vk::QueryType::eTimestamp, 2);
		buffer->queryPool = vulkanAPI->device.createQueryPool(queryPoolInfo);
		#endif

		buffers[i] = buffer;
	}

	return buffers;
}
static void destroyVkSwapchainBuffers(VulkanAPI* vulkanAPI, const vector<Swapchain::Buffer*>& buffers)
{
	for (auto buffer : buffers)
	{
		auto vkBuffer = dynamic_cast<VulkanSwapchain::VkBuffer*>(buffer);
		#if GARDEN_DEBUG || GARDEN_EDITOR
		vulkanAPI->device.destroyQueryPool(vkBuffer->queryPool);
		#endif
		for (auto commandPool : vkBuffer->secondaryCommandPools)
			vulkanAPI->device.destroyCommandPool(commandPool);
		auto imageView = vulkanAPI->imagePool.get(vkBuffer->colorImage);
		if (imageView->hasDefaultView())
			vulkanAPI->imageViewPool.destroy(imageView->getDefaultView());
		vulkanAPI->imagePool.destroy(vkBuffer->colorImage);
		delete vkBuffer;
	}
}

//**********************************************************************************************************************
VulkanSwapchain::VulkanSwapchain(VulkanAPI* vulkanAPI, uint2 framebufferSize, bool useVsync,
	bool useTripleBuffering) : Swapchain(framebufferSize, useVsync, useTripleBuffering)
{
	this->vulkanAPI = vulkanAPI;

	vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled);
	vk::SemaphoreCreateInfo semaphoreInfo;

	for (uint8 i = 0; i < frameLag; i++)
	{
		fences[i] = vulkanAPI->device.createFence(fenceInfo);
		imageAcquiredSemaphores[i] = vulkanAPI->device.createSemaphore(semaphoreInfo);
		drawCompleteSemaphores[i] = vulkanAPI->device.createSemaphore(semaphoreInfo);
	}

	vk::Format format;
	instance = createVkSwapchain(vulkanAPI, framebufferSize, useVsync, useTripleBuffering, nullptr, format);
	buffers = createVkSwapchainBuffers(vulkanAPI, instance, framebufferSize, format);
}
VulkanSwapchain::~VulkanSwapchain()
{
	destroyVkSwapchainBuffers(vulkanAPI, buffers);
	vulkanAPI->device.destroySwapchainKHR(instance);

	for (uint8 i = 0; i < frameLag; i++)
	{
		vulkanAPI->device.destroySemaphore(drawCompleteSemaphores[i]);
		vulkanAPI->device.destroySemaphore(imageAcquiredSemaphores[i]);
		vulkanAPI->device.destroyFence(fences[i]);
	}
}

//**********************************************************************************************************************
void VulkanSwapchain::recreate(uint2 framebufferSize, bool useVsync, bool useTripleBuffering)
{
	vulkanAPI->device.waitIdle();
	destroyVkSwapchainBuffers(vulkanAPI, buffers);

	vk::Format format;
	auto newInstance = createVkSwapchain(vulkanAPI, framebufferSize, useVsync, useTripleBuffering, instance, format);
	vulkanAPI->device.destroySwapchainKHR(instance);
	buffers = createVkSwapchainBuffers(vulkanAPI, newInstance, framebufferSize, format);

	this->framebufferSize = framebufferSize;
	this->useVsync = useVsync;
	this->useTripleBuffering = useTripleBuffering;
	this->instance = newInstance;
	this->frameIndex = 0;
}

//**********************************************************************************************************************
bool VulkanSwapchain::acquireNextImage(ThreadPool* threadPool)
{
	SET_CPU_ZONE_SCOPED("Next Image Acquire");

	auto fence = fences[frameIndex];
	auto waitResult = vulkanAPI->device.waitForFences(1, &fence, VK_TRUE, 10000000000); // Note: emergency 10 seconds timeout.
	vk::detail::resultCheck(waitResult, "vk::Device::waitForFences");
	vulkanAPI->device.resetFences(fence);
	
	auto result = vulkanAPI->device.acquireNextImageKHR(instance,
		UINT64_MAX, imageAcquiredSemaphores[frameIndex], {}, &bufferIndex);
		
	if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
		return false;
	else if (result != vk::Result::eSuccess)
		throw GardenError("Failed to acquire next image. (error: " + vk::to_string(result) + ")");
	// TODO: recreate surface and swapchain on vk::Result::eErrorSurfaceLostKHR 

	auto buffer = dynamic_cast<VulkanSwapchain::VkBuffer*>(buffers[bufferIndex]);
	if (threadPool)
	{
		threadPool->addTasks(ThreadPool::Task([this, buffer](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Command Pool Reset");
			vulkanAPI->device.resetCommandPool(buffer->secondaryCommandPools[task.getTaskIndex()]);
		}),
		(uint32)buffer->secondaryCommandPools.size());
		threadPool->wait();
	}
	else
	{
		for (uint32 i = 0; i < buffer->secondaryCommandPools.size(); i++)
			vulkanAPI->device.resetCommandPool(buffer->secondaryCommandPools[i]);
	}

	buffer->secondaryCommandBufferIndex = 0;
	vmaSetCurrentFrameIndex(vulkanAPI->memoryAllocator, bufferIndex);
	this->threadPool = threadPool;
	return true;
}

//**********************************************************************************************************************
void VulkanSwapchain::submit()
{
	auto buffer = dynamic_cast<VulkanSwapchain::VkBuffer*>(buffers[bufferIndex]);
	vk::PipelineStageFlags pipelineStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	vk::SubmitInfo submitInfo(1, &imageAcquiredSemaphores[frameIndex], &pipelineStage, 
		1, &buffer->primaryCommandBuffer, 1, &drawCompleteSemaphores[frameIndex]);
	vulkanAPI->frameQueue.submit(submitInfo, fences[frameIndex]);
}
bool VulkanSwapchain::present()
{
	vk::PresentInfoKHR presentInfo(1, &drawCompleteSemaphores[frameIndex], 1, &instance, &bufferIndex);
	auto result = vulkanAPI->frameQueue.presentKHR(&presentInfo); // & is required here.
	frameIndex = (frameIndex + 1) % frameLag;

	if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
		return false;
	else if (result != vk::Result::eSuccess)
		throw GardenError("Failed to present image. (error: " + vk::to_string(result) + ")");
	return true;
}

//**********************************************************************************************************************
void VulkanSwapchain::beginSecondaryCommandBuffers(vk::Framebuffer framebuffer, vk::RenderPass renderPass, 
	uint8 subpassIndex, const vector<Framebuffer::OutputAttachment>& colorAttachments,
	Framebuffer::OutputAttachment depthStencilAttachment, const string& name)
{
	SET_CPU_ZONE_SCOPED("Secondary Command Buffers Begin");

	auto threadCount = vulkanAPI->threadCount;
	vulkanAPI->secondaryCommandBuffers.resize(threadCount);
	vulkanAPI->secondaryCommandStates.resize(threadCount);
	
	auto buffer = dynamic_cast<VulkanSwapchain::VkBuffer*>(buffers[bufferIndex]);
	if (buffer->secondaryCommandBufferIndex < buffer->secondaryCommandBuffers.size())
	{
		memcpy(vulkanAPI->secondaryCommandBuffers.data(),
			buffer->secondaryCommandBuffers.data() + buffer->secondaryCommandBufferIndex,
			threadCount * sizeof(vk::CommandBuffer));
	}
	else
	{
		const auto& secondaryCommandPools = buffer->secondaryCommandPools;
		buffer->secondaryCommandBuffers.resize(buffer->secondaryCommandBufferIndex + threadCount);
		auto secondaryCommandBuffers = buffer->secondaryCommandBuffers.data() + buffer->secondaryCommandBufferIndex;
		vk::CommandBufferAllocateInfo allocateInfo({}, vk::CommandBufferLevel::eSecondary, 1);
		
		for (uint32 i = 0; i < threadCount; i++)
		{
			allocateInfo.commandPool = secondaryCommandPools[i];
			vk::CommandBuffer commandBuffer;
			auto allocateResult = vulkanAPI->device.allocateCommandBuffers(&allocateInfo, &commandBuffer);
			vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
			secondaryCommandBuffers[i] = commandBuffer;
			vulkanAPI->secondaryCommandBuffers[i] = commandBuffer;

			#if GARDEN_DEBUG
			if (vulkanAPI->hasDebugUtils)
			{
				auto objectName = name + ".secondaryCommandBuffer" + to_string(i);
				vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eCommandBuffer,
					(uint64)(VkCommandBuffer)commandBuffer, objectName.c_str());
				vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
			}
			#endif
		}
	}

	buffer->secondaryCommandBufferIndex += threadCount;

	for (uint32 i = 0; i < threadCount; i++)
		vulkanAPI->secondaryCommandStates[i] = false;

	vk::CommandBufferInheritanceInfo inheritanceInfo(renderPass, subpassIndex, framebuffer, VK_FALSE); // TODO: occlusion query
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
		vk::CommandBufferUsageFlagBits::eRenderPassContinue, &inheritanceInfo);

	vk::CommandBufferInheritanceRenderingInfoKHR inheritanceRenderingInfo;
	if (!renderPass)
	{
		if (colorAttachmentFormats.size() < colorAttachments.size())
			colorAttachmentFormats.resize(colorAttachments.size());

		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			auto imageView = vulkanAPI->imageViewPool.get(colorAttachments[i].imageView);
			colorAttachmentFormats[i] = toVkFormat(imageView->getFormat());
		}

		if (depthStencilAttachment.imageView)
		{
			auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
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

	threadPool->addTasks(ThreadPool::Task([this, &beginInfo](const ThreadPool::Task& task)
	{
		SET_CPU_ZONE_SCOPED("Secondary Command Buffer Begin");
		vulkanAPI->secondaryCommandBuffers[task.getTaskIndex()].begin(beginInfo);
	}),
	threadCount);
	threadPool->wait();
}

//**********************************************************************************************************************
void VulkanSwapchain::endSecondaryCommandBuffers()
{
	SET_CPU_ZONE_SCOPED("Secondary Command Buffers End");

	threadPool->addTasks(ThreadPool::Task([this](const ThreadPool::Task& task)
	{
		SET_CPU_ZONE_SCOPED("Secondary Command Buffer End");
		vulkanAPI->secondaryCommandBuffers[task.getTaskIndex()].end();
	}),
	(uint32)vulkanAPI->secondaryCommandBuffers.size());
	threadPool->wait();

	GARDEN_ASSERT(vulkanAPI->secondaryCommandStates.size() == vulkanAPI->secondaryCommandBuffers.size());
	for (uint16 i = 0; i < (uint16)vulkanAPI->secondaryCommandBuffers.size(); i++)
	{
		if (vulkanAPI->secondaryCommandStates[i])
			secondaryCommandBuffers.push_back(vulkanAPI->secondaryCommandBuffers[i]);
	}

	if (!secondaryCommandBuffers.empty())
	{
		ExecuteCommand command;
		command.bufferCount = (uint16)secondaryCommandBuffers.size();
		command.buffers = secondaryCommandBuffers.data();
		vulkanAPI->currentCommandBuffer->addCommand(command);
		secondaryCommandBuffers.clear();
	}

	vulkanAPI->secondaryCommandBuffers.clear();
}