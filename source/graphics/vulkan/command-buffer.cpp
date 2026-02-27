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

#include "garden/graphics/vulkan/command-buffer.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/profiler.hpp"

using namespace math;
using namespace garden;

static vk::CommandBuffer createVkCommandBuffer(vk::Device device, vk::CommandPool commandPool)
{
	vk::CommandBufferAllocateInfo commandBufferInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::CommandBuffer commandBuffer;
	auto allocateResult = device.allocateCommandBuffers(&commandBufferInfo, &commandBuffer);
	vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
	return commandBuffer;
}

//**********************************************************************************************************************
VulkanCommandBuffer::VulkanCommandBuffer(VulkanAPI* vulkanAPI, CommandBufferType type) : 
	CommandBuffer(vulkanAPI->getThreadPool(), type), vulkanAPI(vulkanAPI)
{
	switch (type)
	{
	case CommandBufferType::Graphics:
		instance = createVkCommandBuffer(vulkanAPI->device, vulkanAPI->graphicsCommandPool); break;
	case CommandBufferType::TransferOnly:
		instance = createVkCommandBuffer(vulkanAPI->device, vulkanAPI->transferCommandPool); break;
	case CommandBufferType::Compute:
		instance = createVkCommandBuffer(vulkanAPI->device, vulkanAPI->computeCommandPool); break;
	case CommandBufferType::Frame: break;
	default: abort();
	}

	#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
	if (vulkanAPI->features.debugUtils)
	{
		const char* name = nullptr;
		switch (type)
		{
			case CommandBufferType::Graphics: name = "commandBuffer.graphics"; break;
			case CommandBufferType::TransferOnly: name = "commandBuffer.transferOnly"; break;
			case CommandBufferType::Compute: name = "commandBuffer.compute"; break;
			case CommandBufferType::AsyncCompute: name = "commandBuffer.asyncCompute"; break;
			case CommandBufferType::Frame: break;
			default: abort();
		}

		if (name)
		{
			vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eCommandBuffer,
				(uint64)(VkCommandBuffer)instance, name);
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		}
	}
	#endif

	if (instance)
	{
		vk::FenceCreateInfo fenceInfo;
		fence = vulkanAPI->device.createFence(fenceInfo);
	}
}
VulkanCommandBuffer::~VulkanCommandBuffer()
{
	vulkanAPI->device.destroyFence(fence);
}

static void fixupLegacyVkState(Memory::BarrierState& state) noexcept
{
	if (state.stage & (uint64)(vk::PipelineStageFlagBits2::eIndexInput |
		vk::PipelineStageFlagBits2::eVertexAttributeInput))
	{
		state.stage |= (uint64)vk::PipelineStageFlagBits2::eVertexInput;
	}
	if (state.stage & (uint64)(vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eResolve |
		vk::PipelineStageFlagBits2::eBlit | vk::PipelineStageFlagBits2::eClear))
	{
		state.stage |= (uint64)vk::PipelineStageFlagBits2::eTransfer;
	}
	if (state.access & (uint64)(vk::AccessFlagBits2::eUniformRead | 
		vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eShaderStorageRead))
	{
		state.access |= (uint64)vk::AccessFlagBits2::eShaderRead;
	}
	if (state.access & (uint64)vk::AccessFlagBits2::eShaderStorageWrite)
	{
		state.access |= (uint64)vk::AccessFlagBits2::eShaderWrite;
	}
}

//**********************************************************************************************************************
static void addMemoryBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& oldState, Buffer::BarrierState& newState)
{
	if (VulkanCommandBuffer::isDifferentState(oldState, newState))
	{
		if (vulkanAPI->features.synchronization2)
		{
			vulkanAPI->memoryBarriers2.push_back(vk::MemoryBarrier2(
				vk::PipelineStageFlags2(oldState.stage), vk::AccessFlags2(oldState.access), 
				vk::PipelineStageFlags2(newState.stage), vk::AccessFlags2(newState.access)));
		}
		else
		{
			fixupLegacyVkState(oldState); fixupLegacyVkState(newState);
			vulkanAPI->memoryBarriers.push_back(vk::MemoryBarrier(
				vk::AccessFlags((uint32)oldState.access), vk::AccessFlags((uint32)newState.access)));
			vulkanAPI->oldPipelineStage |= (uint32)oldState.stage;
			vulkanAPI->newPipelineStage |= (uint32)newState.stage;
		}
	}
	oldState = newState;
}
static void addMemoryBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& srcOldState, 
	Buffer::BarrierState& dstOldState, Buffer::BarrierState& srcNewState, Buffer::BarrierState dstNewState)
{
	if (VulkanCommandBuffer::isDifferentState(srcOldState, srcNewState) || 
		VulkanCommandBuffer::isDifferentState(dstOldState, dstNewState))
	{
		if (vulkanAPI->features.synchronization2)
		{
			vulkanAPI->memoryBarriers2.push_back(vk::MemoryBarrier2(
				vk::PipelineStageFlags2(srcOldState.stage | dstOldState.stage), 
				vk::AccessFlags2(srcOldState.access | dstOldState.access), 
				vk::PipelineStageFlags2(srcNewState.stage | dstNewState.stage), 
				vk::AccessFlags2(srcNewState.access | dstNewState.access)));
		}
		else
		{
			fixupLegacyVkState(srcOldState); fixupLegacyVkState(srcNewState);
			vulkanAPI->memoryBarriers.push_back(vk::MemoryBarrier(
				vk::AccessFlags((uint32)srcOldState.access | (uint32)dstOldState.access), 
				vk::AccessFlags((uint32)srcNewState.access | (uint32)dstNewState.access)));
			vulkanAPI->oldPipelineStage |= (uint32)srcOldState.stage | (uint32)dstOldState.stage;
			vulkanAPI->newPipelineStage |= (uint32)srcNewState.stage | (uint32)dstNewState.stage;
		}
	}

	srcOldState = srcNewState;
	dstOldState = dstNewState;
}

static void addBufferBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& oldState, 
	Buffer::BarrierState& newState, VkBuffer vkBuffer, uint64 size, uint64 offset)
{
	if (vulkanAPI->features.synchronization2)
	{
		vulkanAPI->bufferMemoryBarriers2.push_back(vk::BufferMemoryBarrier2(
			vk::PipelineStageFlags2(oldState.stage), vk::AccessFlags2(oldState.access), 
			vk::PipelineStageFlags2(newState.stage), vk::AccessFlags2(newState.access),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkBuffer, offset, size));
	}
	else
	{
		fixupLegacyVkState(oldState); fixupLegacyVkState(newState);
		vulkanAPI->bufferMemoryBarriers.push_back(vk::BufferMemoryBarrier(
			vk::AccessFlags((uint32)oldState.access), vk::AccessFlags((uint32)newState.access),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkBuffer, offset, size));
		vulkanAPI->oldPipelineStage |= (uint32)oldState.stage;
		vulkanAPI->newPipelineStage |= (uint32)newState.stage;
	}
}
void VulkanCommandBuffer::addBufferBarrier(VulkanAPI* vulkanAPI, 
	Buffer::BarrierState& newBufferState, ID<Buffer> buffer, uint64 size, uint64 offset)
{
	// TODO: we can specify only required buffer range, not full range.
	auto& oldBufferState = vulkanAPI->getBufferState(buffer);
	if (isDifferentState(oldBufferState, newBufferState))
	{
		auto bufferView = vulkanAPI->bufferPool.get(buffer);
		::addBufferBarrier(vulkanAPI, oldBufferState, newBufferState, 
			(VkBuffer)ResourceExt::getInstance(**bufferView), size, offset);
	}
	oldBufferState = newBufferState;
}

//**********************************************************************************************************************
static void addImageBarrier(VulkanAPI* vulkanAPI, Image::LayoutState& oldState, 
	Image::LayoutState& newState, VkImage vkImage, uint32 baseMip, uint32 mipCount, 
	uint32 baseLayer, uint32 layerCount, vk::ImageAspectFlags aspectFlags)
{
	vk::ImageSubresourceRange subresourceRange(aspectFlags, baseMip, mipCount, baseLayer, layerCount);
	if (vulkanAPI->features.synchronization2)
	{
		vulkanAPI->imageMemoryBarriers2.push_back(vk::ImageMemoryBarrier2(
			vk::PipelineStageFlags2(oldState.stage), vk::AccessFlags2(oldState.access),
			vk::PipelineStageFlags2(newState.stage), vk::AccessFlags2(newState.access),
			vk::ImageLayout(oldState.layout), vk::ImageLayout(newState.layout),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange));
	}
	else
	{
		fixupLegacyVkState(oldState); fixupLegacyVkState(newState);
		vulkanAPI->imageMemoryBarriers.push_back(vk::ImageMemoryBarrier(
			vk::AccessFlags((uint32)oldState.access), vk::AccessFlags((uint32)newState.access),
			vk::ImageLayout(oldState.layout), vk::ImageLayout(newState.layout),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange));
		vulkanAPI->oldPipelineStage |= (uint32)oldState.stage;
		vulkanAPI->newPipelineStage |= (uint32)newState.stage;
	}
}
void VulkanCommandBuffer::addImageBarrier(VulkanAPI* vulkanAPI, 
	Image::LayoutState& newImageState, ID<ImageView> imageView)
{
	auto view = vulkanAPI->imageViewPool.get(imageView);
	auto image = vulkanAPI->imagePool.get(view->getImage());
	auto& oldImageState = vulkanAPI->getImageState(view->getImage(), view->getBaseMip(), view->getBaseLayer());

	if (VulkanCommandBuffer::isDifferentState(oldImageState, newImageState))
	{
		::addImageBarrier(vulkanAPI, oldImageState, newImageState, (VkImage)ResourceExt::getInstance(**image), 
			view->getBaseMip(), 1, view->getBaseLayer(), 1, toVkImageAspectFlags(view->getFormat()));
	}

	imageView = oldImageState.view;
	oldImageState = newImageState;
	oldImageState.view = imageView;

	ImageExt::isFullBarrier(**image) = image->getMipCount() == 1 && image->getLayerCount() == 1;
}

static void addImageBarriers(VulkanAPI* vulkanAPI, Image::LayoutState& newImageState, 
	ID<Image> image, uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	auto imageView = vulkanAPI->imagePool.get(image);
	auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
	auto aspectFlags = toVkImageAspectFlags(imageView->getFormat());

	auto newFullBarrier = baseMip == 0 && mipCount == imageView->getMipCount() &&
		baseLayer == 0 && layerCount == imageView->getLayerCount();
	if (ImageExt::isFullBarrier(**imageView) && newFullBarrier)
	{
		auto& oldImageState = vulkanAPI->getImageState(image, 0, 0);
		if (VulkanCommandBuffer::isDifferentState(oldImageState, newImageState))
		{
			addImageBarrier(vulkanAPI, oldImageState, newImageState, 
				vkImage, 0, mipCount, 0, layerCount, aspectFlags);
		}

		auto& barrierStates = ImageExt::getBarrierStates(**imageView);
		for (auto& oldBarrierState : barrierStates)
		{
			auto imageView = oldBarrierState.view;
			oldBarrierState = newImageState;
			oldBarrierState.view = imageView;
		}
	}
	else
	{
		mipCount += baseMip; layerCount += baseLayer;
		for (uint8 mip = baseMip; mip < mipCount; mip++)
		{
			for (uint32 layer = baseLayer; layer < layerCount; layer++)
			{
				auto& oldImageState = vulkanAPI->getImageState(image, mip, layer);
				if (VulkanCommandBuffer::isDifferentState(oldImageState, newImageState))
				{
					addImageBarrier(vulkanAPI, oldImageState, newImageState, 
						vkImage, mip, 1, layer, 1, aspectFlags);
				}

				auto imageView = oldImageState.view;
				oldImageState = newImageState;
				oldImageState.view = imageView;
			}
		}
	}
	ImageExt::isFullBarrier(**imageView) = newFullBarrier;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addDescriptorSetBarriers(VulkanAPI* vulkanAPI, 
	const DescriptorSet::Range* descriptorSetRanges, uint32 rangeCount)
{
	SET_CPU_ZONE_SCOPED("Descriptor Set Barriers Add");

	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRanges[i];
		auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptor.set);
		const auto& dsUniforms = descriptorSet->getUniforms();
		auto pipelineView = vulkanAPI->getPipelineView(
			descriptorSet->getPipelineType(), descriptorSet->getPipeline());
		const auto& pipelineUniforms = pipelineView->getUniforms();

		for (const auto& pipelineUniform : pipelineUniforms)
		{
			auto uniform = pipelineUniform.second;
			if (uniform.descriptorSetIndex != i || uniform.isNoncoherent)
				continue;

			auto setCount = descriptor.offset + descriptor.count;
			const auto& dsUniform = dsUniforms.at(pipelineUniform.first);

			if (isSamplerType(uniform.type) || isImageType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Image/Sampler Barriers Process");

				Image::LayoutState newImageState;
				newImageState.stage = (uint64)toVkPipelineStages(uniform.pipelineStages);

				if (isSamplerType(uniform.type))
				{
					newImageState.access = (uint64)vk::AccessFlagBits2::eShaderSampledRead;
					newImageState.layout = (uint32)vk::ImageLayout::eShaderReadOnlyOptimal;
				}
				else
				{
					if (uniform.readAccess)
						newImageState.access = (uint64)vk::AccessFlagBits2::eShaderStorageRead;
					if (uniform.writeAccess)
						newImageState.access |= (uint64)vk::AccessFlagBits2::eShaderStorageWrite;
					newImageState.layout = (uint32)vk::ImageLayout::eGeneral;
				}

				if (pipelineView->isBindless())
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = dsUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							if (!resource)
								continue;

							auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), imageView->getBaseMip(), 
								imageView->getMipCount(), imageView->getBaseLayer(), imageView->getLayerCount());
						}
					}
				}
				else
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = dsUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), imageView->getBaseMip(), 
								imageView->getMipCount(), imageView->getBaseLayer(), imageView->getLayerCount());
						}
					}
				}
			}
			else if (isBufferType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Buffer Barriers Process");

				Buffer::BarrierState newBufferState;
				newBufferState.stage = (uint64)toVkPipelineStages(uniform.pipelineStages);

				if (uniform.type == GslUniformType::UniformBuffer)
				{
					newBufferState.access = (uint64)vk::AccessFlagBits2::eUniformRead;
				}
				else
				{
					if (uniform.readAccess)
						newBufferState.access = (uint64)vk::AccessFlagBits2::eShaderStorageRead;
					if (uniform.writeAccess)
						newBufferState.access |= (uint64)vk::AccessFlagBits2::eShaderStorageWrite;
				}

				if (pipelineView->isBindless())
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = dsUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							if (!resource)
								continue;
							addBufferBarrier(vulkanAPI, newBufferState, ID<Buffer>(resource));
						}
					}
				}
				else
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = dsUniform.resourceSets[j];
						for (auto resource : resourceArray)
							addBufferBarrier(vulkanAPI, newBufferState, ID<Buffer>(resource));
					}
				}
			}
			else if (uniform.type == GslUniformType::AccelerationStructure)
			{
				Buffer::BarrierState newAsState;
				newAsState.access = (uint64)vk::AccessFlagBits2::eAccelerationStructureReadKHR;
				newAsState.stage = (uint64)vk::PipelineStageFlagBits2::eRayTracingShaderKHR;

				for (uint32 j = descriptor.offset; j < setCount; j++)
				{
					const auto& resourceArray = dsUniform.resourceSets[j];
					for (auto resource : resourceArray)
					{
						auto tlasView = vulkanAPI->tlasPool.get(ID<Tlas>(resource));
						auto& oldAsState = AccelerationStructureExt::getBarrierState(**tlasView);
						addMemoryBarrier(vulkanAPI, oldAsState, newAsState);
					}
				}
			}
			else abort();
		}
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processPipelineBarriers()
{
	SET_CPU_ZONE_SCOPED("Pipeline Barriers Process");

	// TODO: combine overlapping barriers.

	if (vulkanAPI->features.synchronization2)
	{
		if (vulkanAPI->imageMemoryBarriers2.empty() && 
			vulkanAPI->bufferMemoryBarriers2.empty() && 
			vulkanAPI->memoryBarriers2.empty())
		{
			return;
		}

		vk::DependencyInfo dependencyInfo(vk::DependencyFlagBits::eByRegion, 
			(uint32)vulkanAPI->memoryBarriers2.size(), vulkanAPI->memoryBarriers2.data(),
			(uint32)vulkanAPI->bufferMemoryBarriers2.size(), vulkanAPI->bufferMemoryBarriers2.data(),
			(uint32)vulkanAPI->imageMemoryBarriers2.size(), vulkanAPI->imageMemoryBarriers2.data());
		instance.pipelineBarrier2(dependencyInfo);

		vulkanAPI->memoryBarriers2.clear();
		vulkanAPI->imageMemoryBarriers2.clear();
		vulkanAPI->bufferMemoryBarriers2.clear();
	}
	else
	{
		if (vulkanAPI->imageMemoryBarriers.empty() && 
			vulkanAPI->bufferMemoryBarriers.empty() && 
			vulkanAPI->memoryBarriers.empty())
		{
			return;
		}

		auto oldPipelineStage = (vk::PipelineStageFlagBits)vulkanAPI->oldPipelineStage;
		auto newPipelineStage = (vk::PipelineStageFlagBits)vulkanAPI->newPipelineStage;

		if (oldPipelineStage == vk::PipelineStageFlagBits::eNone)
			oldPipelineStage = vk::PipelineStageFlagBits::eTopOfPipe;

		instance.pipelineBarrier(oldPipelineStage, newPipelineStage, vk::DependencyFlagBits::eByRegion, 
			(uint32)vulkanAPI->memoryBarriers.size(), vulkanAPI->memoryBarriers.data(),
			(uint32)vulkanAPI->bufferMemoryBarriers.size(), vulkanAPI->bufferMemoryBarriers.data(),
			(uint32)vulkanAPI->imageMemoryBarriers.size(), vulkanAPI->imageMemoryBarriers.data());

		vulkanAPI->memoryBarriers.clear();
		vulkanAPI->imageMemoryBarriers.clear();
		vulkanAPI->bufferMemoryBarriers.clear();
		vulkanAPI->oldPipelineStage = vulkanAPI->newPipelineStage = 0;
	}	
}

//**********************************************************************************************************************
void VulkanCommandBuffer::submit()
{
	SET_CPU_ZONE_SCOPED("Command Buffer Submit");
	auto swapchain = vulkanAPI->vulkanSwapchain;
	
	vk::Queue queue;
	if (type == CommandBufferType::Frame)
	{
		auto& inFlightFrame = swapchain->getInFlightFrame();
		instance = inFlightFrame.primaryCommandBuffer;
	}
	else
	{
		if (isRunning)
		{
			// TODO: this approach involves stall bubbles.
			// buffer can be busy at the frame start and became free at middle,
			// when we are vsync locked already. Suboptimal.

			vk::Fence fence((VkFence)this->fence);
			auto status = vulkanAPI->device.getFenceStatus(fence);
			if (status == vk::Result::eNotReady)
				return;
			
			vulkanAPI->device.resetFences(fence);
			flushLockedResources(lockedResources);
			isRunning = false;
		}

		if (!hasAnyCommand)
		{
			size = lastSize = 0;
			return;
		}

		if (type == CommandBufferType::TransferOnly)
			queue = vulkanAPI->transferQueue;
		else if (type == CommandBufferType::Compute)
			queue = vulkanAPI->computeQueue;
		else
			queue = vulkanAPI->graphicsQueue;
	}

	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	instance.begin(beginInfo);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (type == CommandBufferType::Frame && vulkanAPI->recordGpuTime)
	{
		auto& inFlightFrame = swapchain->getInFlightFrame();
		auto queryPool = inFlightFrame.queryPool;
		instance.resetQueryPool(queryPool, 0, 2);
		if (vulkanAPI->features.synchronization2)
			instance.writeTimestamp2(vk::PipelineStageFlagBits2::eNone, queryPool, 0);
		else instance.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPool, 0);
		inFlightFrame.isPoolClean = true;
		// TODO: record transfer and compute command buffer perf.
	}
	#endif

	processCommands();

	if (type == CommandBufferType::Frame)
	{
		auto swapchainImage = swapchain->getCurrentImage();
		auto imageView = vulkanAPI->imagePool.get(swapchainImage);
		auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
		auto& oldImageState = vulkanAPI->getImageState(swapchainImage, 0, 0);

		if (!hasAnyCommand)
		{
			Image::LayoutState newImageState;
			newImageState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
			newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;

			vk::ImageSubresourceRange subresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
			if (vulkanAPI->features.synchronization2)
			{
				newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eClear;
				vk::ImageMemoryBarrier2 imageMemoryBarrier(
					vk::PipelineStageFlagBits2::eNone, vk::AccessFlags2(oldImageState.access), 
					vk::PipelineStageFlags2(newImageState.stage), vk::AccessFlags2(newImageState.access),
					(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange);
				vk::DependencyInfo dependencyInfo(vk::DependencyFlagBits::eByRegion, 
					0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				instance.pipelineBarrier2(dependencyInfo);
			}
			else
			{
				newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eTransfer;
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
					(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange);
				instance.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlags(
					newImageState.stage), vk::DependencyFlagBits::eByRegion, {}, {}, imageMemoryBarrier);
			}

			auto imageView = oldImageState.view;
			oldImageState = newImageState;
			oldImageState.view = imageView;

			array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
			instance.clearColorImage(vkImage, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(clearColor),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		}

		Image::LayoutState newImageState;
		newImageState.access = (uint64)vk::AccessFlagBits2::eNone;
		newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eBottomOfPipe;
		newImageState.layout = (uint32)vk::ImageLayout::ePresentSrcKHR;

		vk::ImageSubresourceRange subresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		if (vulkanAPI->features.synchronization2)
		{
			vk::ImageMemoryBarrier2 imageMemoryBarrier(
				vk::PipelineStageFlags2(oldImageState.stage), vk::AccessFlags2(oldImageState.access), 
				vk::PipelineStageFlags2(newImageState.stage), vk::AccessFlags2(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange);
			vk::DependencyInfo dependencyInfo(vk::DependencyFlagBits::eByRegion, 
				0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			instance.pipelineBarrier2(dependencyInfo);
		}
		else
		{
			auto oldImageStage = oldImageState.stage == (uint64)vk::PipelineStageFlagBits2::eNone ?
				(uint64)vk::PipelineStageFlagBits2::eTopOfPipe : oldImageState.stage;
			vk::ImageMemoryBarrier imageMemoryBarrier(
				vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage, subresourceRange);
			instance.pipelineBarrier(vk::PipelineStageFlagBits(oldImageStage), vk::PipelineStageFlags(
				newImageState.stage), vk::DependencyFlagBits::eByRegion, {}, {}, imageMemoryBarrier);
		}
		oldImageState = newImageState;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (vulkanAPI->recordGpuTime)
		{
			auto queryPool = swapchain->getInFlightFrame().queryPool;
			if (vulkanAPI->features.synchronization2)
				instance.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, queryPool, 1);
			else instance.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, queryPool, 1);
		}
		#endif
	}

	GARDEN_ASSERT(vulkanAPI->asBuildData.empty());
	instance.end();

	if (type == CommandBufferType::Frame)
	{
		swapchain->submit();
	}
	else
	{
		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &instance;
		queue.submit(submitInfo, (VkFence)fence);
	}

	std::swap(lockingResources, lockedResources);
	size = lastSize = 0;
	hasAnyCommand = false;
	isRunning = true;
}

bool VulkanCommandBuffer::isBusy()
{
	SET_CPU_ZONE_SCOPED("Is Command Buffer Busy");

	if (type == CommandBufferType::Frame)
		return false;
	vk::Fence fence((VkFence)this->fence);
	return vulkanAPI->device.getFenceStatus(fence) == vk::Result::eNotReady;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addRenderPassBarriers(const Command* command)
{
	auto commandType = command->type;
	if (commandType == Command::Type::BindDescriptorSets)
	{
		auto bindDescriptorSetsCommand = (const BindDescriptorSetsCommand*)command;
		auto descriptorSetRange = (const DescriptorSet::Range*)(
			(const uint8*)command + sizeof(BindDescriptorSetsCommandBase));
		addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand->rangeCount);
		return;
	}
	if (commandType == Command::Type::Draw)
	{
		auto drawCommand = (const DrawCommand*)command;
		if (!drawCommand->vertexBuffer)
			return;

		Buffer::BarrierState newBufferState;
		newBufferState.access = (uint64)vk::AccessFlagBits2::eVertexAttributeRead;
		newBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eVertexAttributeInput;
		addBufferBarrier(vulkanAPI, newBufferState, drawCommand->vertexBuffer);
		return;
	}
	if (commandType == Command::Type::DrawIndexed)
	{
		auto drawIndexedCommand = (const DrawIndexedCommand*)command;
			
		Buffer::BarrierState newBufferState;
		newBufferState.access = (uint64)vk::AccessFlagBits2::eVertexAttributeRead;
		newBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eVertexAttributeInput;
		addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand->vertexBuffer);

		newBufferState.access = (uint64)vk::AccessFlagBits2::eIndexRead;
		newBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eIndexInput;
		addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand->indexBuffer);
		return;
	}
}
void VulkanCommandBuffer::addRenderPassBarriers(uint32 thisSize)
{
	SET_CPU_ZONE_SCOPED("Render Pass Barriers Add");

	auto dataIter = this->dataIter + thisSize;
	while (dataIter < this->dataEnd)
	{
		auto command = (const Command*)dataIter;
		GARDEN_ASSERT(command->type < Command::Type::Count);
		GARDEN_ASSERT(command->type != Command::Type::Unknown);

		if (command->type == Command::Type::EndRenderPass)
			break;

		addRenderPassBarriers(command);
		dataIter += command->thisSize;
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addRenderPassBarriersAsync(uint32 thisSize)
{
	SET_CPU_ZONE_SCOPED("Async Render Pass Barriers Add");

	auto dataIter = this->dataIter + thisSize;
	while (dataIter < this->dataEnd)
	{
		auto command = (const ExecuteCommand*)(this->dataIter + thisSize);
		GARDEN_ASSERT(command->type < Command::Type::Count);
		GARDEN_ASSERT(command->type != Command::Type::Unknown);

		auto commandType = command->type;
		if (commandType == Command::Type::EndRenderPass)
			break;
		dataIter += command->thisSize;

		if (commandType != Command::Type::Execute)
			continue;

		auto asyncCommands = (const uint8*)command + 
			sizeof(ExecuteCommandBase) + command->bufferCount * sizeof(void*);
		auto asyncCommandCount = command->asyncCommandCount;

		// TODO: we can multithread this, but somehow synchronize image/buffer state accesses.
		for (uint32 i = 0; i < asyncCommandCount; i++)
		{
			auto asyncCommand = (const AsyncRenderCommand*)((asyncCommands + 
				i * asyncCommandSize) - asyncCommandOffset);
			addRenderPassBarriers(&asyncCommand->base);
		}
		dataIter += command->thisSize;
	}
}

//**********************************************************************************************************************
static bool findLastSubpassInput(const vector<Framebuffer::Subpass>& subpasses,
	ID<ImageView> colorAttachment, PipelineStage& pipelineStages) noexcept
{
	// TODO: cache this and do not search each frame?
	bool isLastInput = false;
	for (auto subpass = subpasses.rbegin(); subpass != subpasses.rend(); subpass++)
	{
		for (const auto& inputAttachment : subpass->inputAttachments)
		{
			if (colorAttachment == inputAttachment.imageView)
			{
				subpass = subpasses.rend() - 1;
				pipelineStages = inputAttachment.pipelineStages;
				isLastInput = true;
				break;
			}
		}
	}
	return isLastInput;
}
static constexpr bool isMajorCommand(Command::Type commandType) noexcept
{
	return commandType == Command::Type::Dispatch | commandType == Command::Type::TraceRays |
		commandType == Command::Type::EndRenderPass;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BufferBarrierCommand& command)
{
	SET_CPU_ZONE_SCOPED("BufferBarrier Command Process");

	auto commandBufferData = (const uint8*)&command;
	auto bufferCount = command.bufferCount; auto newState = command.newState;
	auto buffers = (const ID<Buffer>*)(commandBufferData + sizeof(BufferBarrierCommandBase));

	for (uint32 i = 0; i < bufferCount; i++) 
		addBufferBarrier(vulkanAPI, newState, buffers[i]);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BeginRenderPassCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginRenderPass Command Process");

	auto framebuffer = vulkanAPI->framebufferPool.get(command.framebuffer);
	const auto& colorAttachments = framebuffer->getColorAttachments().data();
	auto colorAttachmentCount = (uint32)framebuffer->getColorAttachments().size();
	auto commandBufferData = (const uint8*)&command;
	auto clearColors = (const float4*)(commandBufferData + sizeof(BeginRenderPassCommandBase));

	noSubpass = framebuffer->getSubpasses().empty();
	if (noSubpass)
	{
		if (vulkanAPI->colorAttachmentInfos.size() < colorAttachmentCount)
			vulkanAPI->colorAttachmentInfos.resize(colorAttachmentCount);
	}

	for (uint32 i = 0; i < colorAttachmentCount; i++)
	{
		auto colorAttachment = colorAttachments[i];
		if (noSubpass && !colorAttachment.imageView)
		{
			vulkanAPI->colorAttachmentInfos[i].imageView = nullptr;
			continue;
		}

		Image::LayoutState newImageState;
		newImageState.access = (uint64)vk::AccessFlagBits2::eColorAttachmentWrite;
		if (colorAttachment.flags.load)
			newImageState.access |= (uint64)vk::AccessFlagBits2::eColorAttachmentRead;
		newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		newImageState.layout = (uint32)vk::ImageLayout::eColorAttachmentOptimal;
		addImageBarrier(vulkanAPI, newImageState, colorAttachment.imageView);

		if (noSubpass)
		{
			vk::AttachmentLoadOp loadOperation;
			vk::ClearValue clearValue;

			if (colorAttachment.flags.clear)
			{
				array<float, 4> color; *(float4*)color.data() = clearColors[i];
				loadOperation = vk::AttachmentLoadOp::eClear;
				clearValue = vk::ClearValue(vk::ClearColorValue(color));
			}
			else if (colorAttachment.flags.load)
			{
				loadOperation = vk::AttachmentLoadOp::eLoad;
			}
			else
			{
				loadOperation = vk::AttachmentLoadOp::eDontCare;
			}

			auto storeOperation = colorAttachment.flags.store ? 
				vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eNone;

			auto imageView = vulkanAPI->imageViewPool.get(colorAttachment.imageView);
			vk::RenderingAttachmentInfo colorAttachmentInfo(
				(VkImageView)ResourceExt::getInstance(**imageView),
				vk::ImageLayout::eColorAttachmentOptimal, {}, nullptr, 
				vk::ImageLayout::eUndefined, loadOperation, storeOperation, clearValue);
			vulkanAPI->colorAttachmentInfos[i] = colorAttachmentInfo;
			// TODO: some how utilize write discarding? (eDontCare)
		}
		else
		{
			array<float, 4> color; *(float4*)color.data() = clearColors[i];
			vulkanAPI->clearValues.emplace_back(vk::ClearColorValue(color));
		}
	}

	vk::RenderingAttachmentInfoKHR depthStencilAttachmentInfo;
	vk::RenderingAttachmentInfoKHR* depthAttachmentInfoPtr = nullptr;
	vk::RenderingAttachmentInfoKHR* stencilAttachmentInfoPtr = nullptr;

	if (framebuffer->getDepthStencilAttachment().imageView)
	{
		auto depthStencilAttachment = framebuffer->getDepthStencilAttachment();

		Image::LayoutState newImageState;
		if (depthStencilAttachment.flags.load)
			newImageState.access = (uint64)vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		if (depthStencilAttachment.flags.clear || depthStencilAttachment.flags.load)
			newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eEarlyFragmentTests;
		if (depthStencilAttachment.flags.store)
		{
			newImageState.access |= (uint64)vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			newImageState.stage |= (uint64)vk::PipelineStageFlagBits2::eLateFragmentTests;
		}

		auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
		auto imageFormat = imageView->getFormat();

		if (noSubpass)
		{
			if (depthStencilAttachment.flags.clear) // TODO: support separated depth/stencil clear?
			{
				depthStencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
				depthStencilAttachmentInfo.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(
					command.clearDepth, command.clearStencil));
			}
			else if (depthStencilAttachment.flags.load)
			{
				depthStencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
			}
			else
			{
				depthStencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eDontCare;
			}

			depthStencilAttachmentInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
			depthStencilAttachmentInfo.storeOp = depthStencilAttachment.flags.store ? 
				vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eNone;
		}
		else
		{
			vulkanAPI->clearValues.emplace_back(vk::ClearDepthStencilValue(
				command.clearDepth, command.clearStencil));
		}

		if (isFormatDepthOnly(imageFormat))
		{
			if (noSubpass)
			{
				depthStencilAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
				depthAttachmentInfoPtr = &depthStencilAttachmentInfo;
			}
			newImageState.layout = (uint32)vk::ImageLayout::eDepthAttachmentOptimal;
		}
		else if (isFormatStencilOnly(imageFormat))
		{
			if (noSubpass)
			{
				depthStencilAttachmentInfo.imageLayout = vk::ImageLayout::eStencilAttachmentOptimal;
				stencilAttachmentInfoPtr = &depthStencilAttachmentInfo;
			}
			newImageState.layout = (uint32)vk::ImageLayout::eStencilAttachmentOptimal;
		}
		else
		{
			if (noSubpass)
			{
				depthStencilAttachmentInfo.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
				depthAttachmentInfoPtr = &depthStencilAttachmentInfo;
				stencilAttachmentInfoPtr = &depthStencilAttachmentInfo;
			}
			newImageState.layout = (uint32)vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}
		addImageBarrier(vulkanAPI, newImageState, depthStencilAttachment.imageView);
	}

	if (command.asyncRecording)
		addRenderPassBarriersAsync(command.thisSize);
	else addRenderPassBarriers(command.thisSize);

	processPipelineBarriers();

	vk::Rect2D rect({ command.region.x, command.region.y },
		{ (uint32)command.region.z, (uint32)command.region.w });

	if (noSubpass)
	{
		vk::RenderingInfo renderingInfo(command.asyncRecording ?
			vk::RenderingFlagBits::eContentsSecondaryCommandBuffers : vk::RenderingFlags(),
			rect, 1, 0, colorAttachmentCount, vulkanAPI->colorAttachmentInfos.data(),
			depthAttachmentInfoPtr, stencilAttachmentInfoPtr);

		if (vulkanAPI->versionMinor < 3)
			instance.beginRenderingKHR(renderingInfo);
		else instance.beginRendering(renderingInfo);
	}
	else
	{
		vk::RenderPassBeginInfo beginInfo((VkRenderPass)FramebufferExt::getRenderPass(**framebuffer),
			(VkFramebuffer)ResourceExt::getInstance(**framebuffer), rect, vulkanAPI->clearValues);
		instance.beginRenderPass(beginInfo, command.asyncRecording ?
			vk::SubpassContents::eSecondaryCommandBuffers : vk::SubpassContents::eInline);
		vulkanAPI->clearValues.clear();

		const auto& subpasses = framebuffer->getSubpasses();
		for (uint32 i = 0; i < colorAttachmentCount; i++)
		{
			auto colorAttachment = colorAttachments[i];
			auto imageView = vulkanAPI->imageViewPool.get(colorAttachment.imageView);
			auto& imageState = vulkanAPI->getImageState(imageView->getImage(),
				imageView->getBaseMip(), imageView->getBaseLayer());
			imageState.layout = (uint32)vk::ImageLayout::eGeneral;

			PipelineStage pipelineStages;
			auto isLastInput = findLastSubpassInput(subpasses, colorAttachment.imageView, pipelineStages);

			if (isLastInput)
			{
				imageState.access = (uint64)vk::AccessFlagBits2::eShaderRead;
				imageState.stage = (uint64)toVkPipelineStages(pipelineStages);
			}
			else
			{
				imageState.access = (uint64)vk::AccessFlagBits2::eColorAttachmentWrite;
				imageState.stage = (uint64)vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			}
		}

		if (framebuffer->getDepthStencilAttachment().imageView)
		{
			auto depthStencilAttachment = framebuffer->getDepthStencilAttachment();
			auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
			auto& imageState = vulkanAPI->getImageState(imageView->getImage(),
				imageView->getBaseMip(), imageView->getBaseLayer());
			imageState.layout = (uint32)vk::ImageLayout::eGeneral;

			PipelineStage pipelineStages;
			auto isLastInput = findLastSubpassInput(subpasses, depthStencilAttachment.imageView, pipelineStages);

			if (isLastInput)
			{
				imageState.access = (uint64)vk::AccessFlagBits2::eShaderRead;
				imageState.stage = (uint64)toVkPipelineStages(pipelineStages);
			}
			else
			{
				imageState.access = (uint64)vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
				imageState.stage = (uint64)vk::PipelineStageFlagBits2::eEarlyFragmentTests;
			}
		}
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const NextSubpassCommand& command)
{
	SET_CPU_ZONE_SCOPED("NextSubpass Command Process");

	instance.nextSubpass(command.asyncRecording ?
		vk::SubpassContents::eSecondaryCommandBuffers : vk::SubpassContents::eInline);
}

void VulkanCommandBuffer::processCommand(const ExecuteCommand& command)
{
	SET_CPU_ZONE_SCOPED("Execute Command Process");

	instance.executeCommands(command.bufferCount, (const vk::CommandBuffer*)
		((const uint8*)&command + sizeof(ExecuteCommandBase)));
}

void VulkanCommandBuffer::processCommand(const EndRenderPassCommand& command)
{
	SET_CPU_ZONE_SCOPED("EndRenderPass Command Process");

	if (noSubpass)
	{
		if (vulkanAPI->versionMinor < 3)
			instance.endRenderingKHR();
		else instance.endRendering();
	}
	else instance.endRenderPass();
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const ClearAttachmentsCommand& command)
{
	SET_CPU_ZONE_SCOPED("ClearAttachments Command Process");

	auto attachmentCount = command.attachmentCount; auto regionCount = command.regionCount;
	auto attachments = (const Framebuffer::ClearAttachment*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase));
	auto regions = (const Framebuffer::ClearRegion*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase) +
		attachmentCount * sizeof(Framebuffer::ClearAttachment));
	const auto framebufferView = vulkanAPI->framebufferPool.get(command.framebuffer);
	const auto& colorAttachments = framebufferView->getColorAttachments();

	if (vulkanAPI->clearAttachments.size() < attachmentCount)
		vulkanAPI->clearAttachments.resize(attachmentCount);
	if (vulkanAPI->clearAttachmentsRects.size() < regionCount)
		vulkanAPI->clearAttachmentsRects.resize(regionCount);

	for (uint8 i = 0; i < attachmentCount; i++)
	{
		auto attachment = attachments[i];

		ID<ImageView> attachmentView;
		if (attachment.index < colorAttachments.size())
			attachmentView = colorAttachments[attachment.index].imageView;
		else if (attachment.index == colorAttachments.size())
		{
			GARDEN_ASSERT_MSG(framebufferView->getDepthStencilAttachment().imageView, 
				"Assert " + framebufferView->getDebugName());
			attachmentView = framebufferView->getDepthStencilAttachment().imageView;
		}
		else abort();

		auto imageView = vulkanAPI->imageViewPool.get(attachmentView);
		auto format = imageView->getFormat();

		vk::ClearValue clearValue;
		if (isFormatFloat(format) | isFormatSrgb(format) | isFormatNorm(format))
			memcpy(clearValue.color.float32.data(), &attachment.clearColor.floatValue, sizeof(float) * 4);
		else if (isFormatSint(format))
			memcpy(clearValue.color.int32.data(), &attachment.clearColor.intValue, sizeof(int32) * 4);
		else if (isFormatUint(format))
			memcpy(clearValue.color.uint32.data(), &attachment.clearColor.uintValue, sizeof(uint32) * 4);
		else
		{
			clearValue.depthStencil.depth = attachment.clearColor.deptStencilValue.depth;
			clearValue.depthStencil.stencil = attachment.clearColor.deptStencilValue.stencil;
		}

		vulkanAPI->clearAttachments[i] = vk::ClearAttachment(
			toVkImageAspectFlags(format), attachment.index, clearValue);
	}

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		
		vk::Rect2D rect({ (int32)region.offset.x, (int32)region.offset.y }, {});
		if (region.extent == uint2::zero)
		{
			auto size = framebufferView->getSize();
			rect.extent.width = size.x;
			rect.extent.height = size.y;
		}
		else
		{
			rect.extent.width = region.extent.x;
			rect.extent.height = region.extent.y;
		}
		
		vulkanAPI->clearAttachmentsRects[i] = vk::ClearRect(rect, 
			region.baseLayer, region.layerCount == 0 ? 1 : region.layerCount);
	}

	// TODO: should we add barriers? Looks like no.
	instance.clearAttachments(attachmentCount, vulkanAPI->clearAttachments.data(),
		regionCount, vulkanAPI->clearAttachmentsRects.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BindPipelineCommand& command)
{	
	SET_CPU_ZONE_SCOPED("BindPipeline Command Process");

	auto pipelineType = command.pipelineType;
	auto pipelineView = vulkanAPI->getPipelineView(pipelineType, command.pipeline);
	auto pipeline = ResourceExt::getInstance(**pipelineView);
	instance.bindPipeline(toVkPipelineBindPoint(pipelineType), pipelineView->getVariantCount() > 1 ? 
		((VkPipeline*)pipeline)[command.variant] : (VkPipeline)pipeline);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BindDescriptorSetsCommand& command)
{
	SET_CPU_ZONE_SCOPED("BindDescriptorSets Command Process");

	auto rangeCount = command.rangeCount;
	auto descriptorSetRanges = (const DescriptorSet::Range*)(
		(const uint8*)&command + sizeof(BindDescriptorSetsCommandBase));
	auto& descriptorSets = vulkanAPI->bindDescriptorSets[0];
	// TODO: maybe detect already bound descriptor sets?

	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRanges[i];
		auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptor.set);
		auto instance = (vk::DescriptorSet*)ResourceExt::getInstance(**descriptorSet);

		if (descriptorSet->getSetCount() > 1)
		{
			auto count = descriptor.offset + descriptor.count;
			for (uint32 j = descriptor.offset; j < count; j++)
				descriptorSets.push_back(instance[j]);
		}
		else descriptorSets.push_back((VkDescriptorSet)instance);
	}

	auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptorSetRanges[0].set);
	auto pipelineView = vulkanAPI->getPipelineView(descriptorSet->getPipelineType(), descriptorSet->getPipeline());
	auto pipelineLayout = (VkPipelineLayout)PipelineExt::getLayout(**pipelineView);

	instance.bindDescriptorSets(toVkPipelineBindPoint(descriptorSet->getPipelineType()),
		pipelineLayout, 0, (uint32)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
	descriptorSets.clear();
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const PushConstantsCommand& command)
{
	SET_CPU_ZONE_SCOPED("PushConstants Command Process");

	instance.pushConstants((VkPipelineLayout)command.pipelineLayout, (vk::ShaderStageFlags)command.pipelineStages,
		0, command.dataSize, (const uint8*)&command + sizeof(PushConstantsCommandBase));
}

void VulkanCommandBuffer::processCommand(const SetViewportCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetViewport Command Process");

	vk::Viewport viewport(command.viewport.x, command.viewport.y,
		command.viewport.z, command.viewport.w, 0.0f, 1.0f); // TODO: depth
	viewport.x = command.framebufferSize.y - (viewport.y + viewport.height);
	instance.setViewport(0, 1, &viewport); // TODO: multiple viewports
}

void VulkanCommandBuffer::processCommand(const SetScissorCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetScissor Command Process");

	vk::Rect2D scissor({ command.scissor.x, command.scissor.y }, 
		{ (uint32)command.scissor.z, (uint32)command.scissor.w });
	scissor.offset.x = command.framebufferSize.y - (scissor.offset.y + scissor.extent.height);
	scissor.offset.x = max(scissor.offset.x, 0); scissor.offset.y = max(scissor.offset.y, 0);
	instance.setScissor(0, 1, &scissor); // TODO: multiple scissors
}

void VulkanCommandBuffer::processCommand(const SetViewportScissorCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetViewportScissor Command Process");

	auto viewportScissor = command.viewportScissor;
	vk::Viewport viewport(viewportScissor.x, viewportScissor.y,
		viewportScissor.z, viewportScissor.w, 0.0f, 1.0f);
	vk::Rect2D scissor({ (int32)viewportScissor.x, (int32)viewportScissor.y },
		{ (uint32)viewportScissor.z, (uint32)viewportScissor.w });
	viewport.x = command.framebufferSize.y - (viewport.y + viewport.height);
	scissor.offset.x = command.framebufferSize.y - (scissor.offset.y + scissor.extent.height);
	scissor.offset.x = max(scissor.offset.x, 0); scissor.offset.y = max(scissor.offset.y, 0);
	instance.setViewport(0, 1, &viewport); instance.setScissor(0, 1, &scissor);
	// TODO: multiple viewports
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DrawCommand& command)
{
	SET_CPU_ZONE_SCOPED("Draw Command Process");

	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	auto vertexBuffer = command.vertexBuffer;
	if (vertexBuffer && vertexBuffer != vulkanAPI->currentVertexBuffers[0])
	{
		constexpr vk::DeviceSize size = 0;
		auto buffer = vulkanAPI->bufferPool.get(vertexBuffer);
		vk::Buffer vkBuffer = (VkBuffer)ResourceExt::getInstance(**buffer);
		instance.bindVertexBuffers(0, 1, &vkBuffer, &size);
		vulkanAPI->currentVertexBuffers[0] = vertexBuffer;
	}

	instance.draw(command.vertexCount, command.instanceCount, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DrawIndexedCommand& command)
{
	SET_CPU_ZONE_SCOPED("DrawIndexed Command Process");

	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	if (command.vertexBuffer != vulkanAPI->currentVertexBuffers[0])
	{
		static constexpr vk::DeviceSize size = 0;
		auto vertexBuffer = command.vertexBuffer;
		auto buffer = vulkanAPI->bufferPool.get(vertexBuffer);
		vk::Buffer vkBuffer = (VkBuffer)ResourceExt::getInstance(**buffer);
		instance.bindVertexBuffers(0, 1, &vkBuffer, &size);
		vulkanAPI->currentVertexBuffers[0] = vertexBuffer;
	}
	if (command.indexBuffer != vulkanAPI->currentIndexBuffers[0])
	{
		auto indexBuffer = command.indexBuffer; auto indexType = command.indexType;
		auto buffer = vulkanAPI->bufferPool.get(indexBuffer);
		instance.bindIndexBuffer((VkBuffer)ResourceExt::getInstance(**buffer),
			(vk::DeviceSize)(command.indexOffset * toBinarySize(indexType)), toVkIndexType(indexType));
		vulkanAPI->currentIndexBuffers[0] = indexBuffer;
	}

	instance.drawIndexed(command.indexCount, command.instanceCount,
		command.indexOffset, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DispatchCommand& command)
{
	SET_CPU_ZONE_SCOPED("Dispatch Command Process");

	auto dataIter = this->dataIter - command.lastSize;
	while (dataIter != this->data)
	{
		auto subCommand = (const Command*)dataIter;
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);
		GARDEN_ASSERT(subCommand->type != Command::Type::Unknown);

		auto commandType = subCommand->type;
		if (isMajorCommand(commandType))
			break;
		dataIter -= subCommand->lastSize;

		if (commandType != Command::Type::BindDescriptorSets)
			continue;

		auto bindDescriptorSetsCommand = (const BindDescriptorSetsCommand*)subCommand;
		auto descriptorSetRange = (const DescriptorSet::Range*)(
			(const uint8*)subCommand + sizeof(BindDescriptorSetsCommandBase));
		addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand->rangeCount);
	}
	processPipelineBarriers();

	instance.dispatch(command.groupCount.x, command.groupCount.y, command.groupCount.z);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const FillBufferCommand& command)
{
	SET_CPU_ZONE_SCOPED("FillBuffer Command Process");

	auto buffer = command.buffer;
	auto bufferView = vulkanAPI->bufferPool.get(buffer);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**bufferView);
	auto size = command.size, offset = command.offset;
	if (size == 0) size = VK_WHOLE_SIZE;

	Buffer::BarrierState newBufferState;
	newBufferState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
	newBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eClear;
	addBufferBarrier(vulkanAPI, newBufferState, buffer, size, offset);
	processPipelineBarriers();

	instance.fillBuffer(vkBuffer, offset, size, command.data);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyBufferCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyBuffer Command Process");

	auto regionCount = command.regionCount;
	auto source = command.source, destination = command.destination;
	auto srcBuffer = vulkanAPI->bufferPool.get(source);
	auto dstBuffer = vulkanAPI->bufferPool.get(destination);
	auto vkSrcBuffer = (VkBuffer)ResourceExt::getInstance(**srcBuffer);
	auto vkDstBuffer = (VkBuffer)ResourceExt::getInstance(**dstBuffer);
	auto regions = (const Buffer::CopyRegion*)((const uint8*)&command + sizeof(CopyBufferCommandBase));

	if (vulkanAPI->bufferCopies.size() < regionCount)
		vulkanAPI->bufferCopies.resize(regionCount);

	Buffer::BarrierState newSrcBufferState;
	newSrcBufferState.access = (uint64)vk::AccessFlagBits2::eTransferRead;
	newSrcBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;

	Buffer::BarrierState newDstBufferState;
	newDstBufferState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
	newDstBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		vulkanAPI->bufferCopies[i] = vk::BufferCopy(region.srcOffset, region.dstOffset,
			region.size == 0 ? srcBuffer->getBinarySize() : region.size);
		addBufferBarrier(vulkanAPI, newSrcBufferState, source);
		addBufferBarrier(vulkanAPI, newDstBufferState, destination);
	}
	processPipelineBarriers();

	instance.copyBuffer(vkSrcBuffer, vkDstBuffer, regionCount, vulkanAPI->bufferCopies.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const ClearImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("ClearImage Command Process");

	auto image = command.image; auto regionCount = command.regionCount;
	auto imageView = vulkanAPI->imagePool.get(image);
	auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
	auto regions = (const Image::ClearRegion*)((const uint8*)&command + sizeof(ClearImageCommandBase));
	auto aspectFlags = toVkImageAspectFlags(imageView->getFormat());

	if (vulkanAPI->imageClears.size() < regionCount)
		vulkanAPI->imageClears.resize(regionCount);

	Image::LayoutState newImageState;
	newImageState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
	newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eClear;
	newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		auto mipCount = region.mipCount == 0 ? imageView->getMipCount() : region.mipCount;
		auto layerCount = region.layerCount == 0 ? imageView->getLayerCount() : region.layerCount;
		vulkanAPI->imageClears[i] = vk::ImageSubresourceRange(aspectFlags,
			region.baseMip, mipCount, region.baseLayer, layerCount);
		addImageBarriers(vulkanAPI, newImageState, image, region.baseMip, mipCount, region.baseLayer, layerCount);
	}
	processPipelineBarriers();

	if (isFormatColor(imageView->getFormat()))
	{
		vk::ClearColorValue clearValue;
		switch(command.clearType)
		{
			case 1: memcpy(clearValue.float32.data(), &command.color, sizeof(float) * 4); break;
			case 2: memcpy(clearValue.int32.data(), &command.color, sizeof(int32) * 4); break;
			case 3: memcpy(clearValue.uint32.data(), &command.color, sizeof(uint32) * 4); break;
			default: abort();
		}

		instance.clearColorImage(vkImage, vk::ImageLayout::eTransferDstOptimal,
			&clearValue, regionCount, vulkanAPI->imageClears.data());
	}
	else
	{
		vk::ClearDepthStencilValue clearValue; clearValue.depth = command.color.x;
		memcpy(&clearValue.stencil, &command.color.y, sizeof(uint32));

		instance.clearDepthStencilImage(vkImage, vk::ImageLayout::eTransferDstOptimal,
			&clearValue, regionCount, vulkanAPI->imageClears.data());
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyImage Command Process");

	auto regionCount = command.regionCount;
	auto source = command.source, destination = command.destination;
	auto srcImage = vulkanAPI->imagePool.get(source);
	auto dstImage = vulkanAPI->imagePool.get(destination);
	auto vkSrcImage = (VkImage)ResourceExt::getInstance(**srcImage);
	auto vkDstImage = (VkImage)ResourceExt::getInstance(**dstImage);
	auto regions = (const Image::CopyImageRegion*)((const uint8*)&command + sizeof(CopyImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->getFormat());
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->getFormat());

	if (vulkanAPI->imageCopies.size() < regionCount)
		vulkanAPI->imageCopies.resize(regionCount);

	Image::LayoutState newSrcImageState;
	newSrcImageState.access = (uint64)vk::AccessFlagBits2::eTransferRead;
	newSrcImageState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;

	Image::LayoutState newDstImageState;
	newDstImageState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
	newDstImageState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		vk::ImageSubresourceLayers srcSubresource(srcAspectFlags, region.srcMipLevel, region.srcBaseLayer);
		vk::ImageSubresourceLayers dstSubresource(dstAspectFlags, region.dstMipLevel, region.dstBaseLayer);
		srcSubresource.layerCount = dstSubresource.layerCount =
			region.layerCount == 0 ? srcImage->getLayerCount() : region.layerCount;
		vk::Offset3D srcOffset(region.srcOffset.x, region.srcOffset.y, region.srcOffset.z);
		vk::Offset3D dstOffset(region.dstOffset.x, region.dstOffset.y, region.dstOffset.z);

		vk::Extent3D extent;
		if (region.extent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(srcImage->getSize(), region.srcMipLevel);
			extent = vk::Extent3D(mipImageSize.getX(), mipImageSize.getY(), 
				srcImage->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else extent = vk::Extent3D(region.extent.x, region.extent.y, region.extent.z);

		vulkanAPI->imageCopies[i] = vk::ImageCopy(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);
		addImageBarriers(vulkanAPI, newSrcImageState, source, region.srcMipLevel, 
			1, region.srcBaseLayer, srcSubresource.layerCount);
		addImageBarriers(vulkanAPI, newDstImageState, destination, region.dstMipLevel, 
			1, region.dstBaseLayer, dstSubresource.layerCount);
	}
	processPipelineBarriers();

	instance.copyImage(vkSrcImage, vk::ImageLayout::eTransferSrcOptimal, vkDstImage, 
		vk::ImageLayout::eTransferDstOptimal, regionCount, vulkanAPI->imageCopies.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyBufferImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyBufferImage Command Process");

	auto regionCount = command.regionCount; auto toBuffer = command.toBuffer;
	auto buffer = command.buffer; auto image = command.image;
	auto bufferView = vulkanAPI->bufferPool.get(buffer);
	auto imageView = vulkanAPI->imagePool.get(image);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**bufferView);
	auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
	auto regions = (const Image::CopyBufferRegion*)((const uint8*)&command + sizeof(CopyBufferImageCommandBase));
	auto aspectFlags = toVkImageAspectFlags(imageView->getFormat());

	if (vulkanAPI->bufferImageCopies.size() < regionCount)
		vulkanAPI->bufferImageCopies.resize(regionCount);

	Buffer::BarrierState newBufferState;
	newBufferState.access = toBuffer ? (uint64)vk::AccessFlagBits2::eTransferWrite : 
		(uint64)vk::AccessFlagBits2::eTransferRead;
	newBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;

	Image::LayoutState newImageState;
	newImageState.access = toBuffer ? (uint64)vk::AccessFlagBits2::eTransferRead : 
		(uint64)vk::AccessFlagBits2::eTransferWrite;
	newImageState.layout = toBuffer ? (uint32)vk::ImageLayout::eTransferSrcOptimal : 
		(uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = (uint64)vk::PipelineStageFlagBits2::eCopy;

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		vk::ImageSubresourceLayers imageSubresource(aspectFlags, region.imageMipLevel, region.imageBaseLayer);
		imageSubresource.layerCount = region.imageLayerCount == 0 ? imageView->getLayerCount() : region.imageLayerCount;
		vk::Offset3D dstOffset(region.imageOffset.x, region.imageOffset.y, region.imageOffset.z);

		vk::Extent3D dstExtent;
		if (region.imageExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(imageView->getSize(), region.imageMipLevel);
			dstExtent = vk::Extent3D(mipImageSize.getX(), mipImageSize.getY(), 
				imageView->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else dstExtent = vk::Extent3D(region.imageExtent.x, region.imageExtent.y, region.imageExtent.z);

		vulkanAPI->bufferImageCopies[i] = vk::BufferImageCopy((vk::DeviceSize)region.bufferOffset,
			region.bufferRowLength, region.bufferImageHeight, imageSubresource, dstOffset, dstExtent);
		addBufferBarrier(vulkanAPI, newBufferState, buffer);
		addImageBarriers(vulkanAPI, newImageState, image, region.imageMipLevel, 
			1, region.imageBaseLayer, imageSubresource.layerCount);
	}
	processPipelineBarriers();

	if (toBuffer)
	{
		instance.copyImageToBuffer(vkImage, vk::ImageLayout::eTransferSrcOptimal,
			vkBuffer, regionCount, vulkanAPI->bufferImageCopies.data());
	}
	else
	{
		instance.copyBufferToImage(vkBuffer, vkImage, vk::ImageLayout::eTransferDstOptimal, 
			regionCount, vulkanAPI->bufferImageCopies.data());
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BlitImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("BlitImage Command Process");

	auto regionCount = command.regionCount;
	auto source = command.source, destination = command.destination;
	auto srcImage = vulkanAPI->imagePool.get(source);
	auto dstImage = vulkanAPI->imagePool.get(destination);
	auto vkSrcImage = (VkImage)ResourceExt::getInstance(**srcImage);
	auto vkDstImage = (VkImage)ResourceExt::getInstance(**dstImage);
	auto regions = (const Image::BlitRegion*)( (const uint8*)&command + sizeof(BlitImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->getFormat());
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->getFormat());

	if (vulkanAPI->imageBlits.size() < regionCount)
		vulkanAPI->imageBlits.resize(regionCount);

	Image::LayoutState newSrcImageState;
	newSrcImageState.access = (uint64)vk::AccessFlagBits2::eTransferRead;
	newSrcImageState.stage = (uint64)vk::PipelineStageFlagBits2::eBlit;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;

	Image::LayoutState newDstImageState;
	newDstImageState.access = (uint64)vk::AccessFlagBits2::eTransferWrite;
	newDstImageState.stage = (uint64)vk::PipelineStageFlagBits2::eBlit;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;

	for (uint32 i = 0; i < regionCount; i++)
	{
		auto region = regions[i];
		vk::ImageSubresourceLayers srcSubresource(srcAspectFlags, region.srcMipLevel, region.srcBaseLayer);
		vk::ImageSubresourceLayers dstSubresource(dstAspectFlags, region.dstMipLevel, region.dstBaseLayer);
		srcSubresource.layerCount = dstSubresource.layerCount =
			region.layerCount == 0 ? srcImage->getLayerCount() : region.layerCount;
		array<vk::Offset3D, 2> srcBounds;
		srcBounds[0] = vk::Offset3D(region.srcOffset.x, region.srcOffset.y, region.srcOffset.z);
		array<vk::Offset3D, 2> dstBounds;
		dstBounds[0] = vk::Offset3D(region.dstOffset.x, region.dstOffset.y, region.dstOffset.z);

		if (region.srcExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(srcImage->getSize(), region.srcMipLevel);
			srcBounds[1] = vk::Offset3D(mipImageSize.getX(), mipImageSize.getY(), 
				srcImage->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else srcBounds[1] = vk::Offset3D(region.srcExtent.x, region.srcExtent.y, region.srcExtent.z);

		if (region.dstExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(dstImage->getSize(), region.dstMipLevel);
			dstBounds[1] = vk::Offset3D(mipImageSize.getX(), mipImageSize.getY(), 
				dstImage->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else dstBounds[1] = vk::Offset3D(region.dstExtent.x, region.dstExtent.y, region.dstExtent.z);

		vulkanAPI->imageBlits[i] = vk::ImageBlit(srcSubresource, srcBounds, dstSubresource, dstBounds);
		addImageBarriers(vulkanAPI, newSrcImageState, source, region.srcMipLevel, 
			1, region.srcBaseLayer, srcSubresource.layerCount);
		addImageBarriers(vulkanAPI, newDstImageState, destination, region.dstMipLevel, 
			1, region.dstBaseLayer, dstSubresource.layerCount);
	}
	processPipelineBarriers();

	instance.blitImage(vkSrcImage, vk::ImageLayout::eTransferSrcOptimal,
		vkDstImage, vk::ImageLayout::eTransferDstOptimal, regionCount, 
		vulkanAPI->imageBlits.data(), (vk::Filter)command.filter);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const SetDepthBiasCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetDepthBias Command Process");
	instance.setDepthBias(command.constantFactor, command.clamp, command.slopeFactor);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BuildAccelerationStructureCommand& command)
{
	SET_CPU_ZONE_SCOPED("BuildAccelerationStructure Command Process");

	auto scratchBufferView = vulkanAPI->bufferPool.get(command.scratchBuffer);
	Buffer::BarrierState srcNewState, dstNewState, tmpState;
	srcNewState.stage = dstNewState.stage = (uint64)vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;

	vk::AccelerationStructureBuildGeometryInfoKHR info;
	OptView<AccelerationStructure> srcAsView = {}, dstAsView = {};
	if (command.srcAS)
	{
		if (command.typeAS == AccelerationStructure::Type::Blas)
			srcAsView = OptView<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.srcAS)));
		else srcAsView = OptView<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.srcAS)));

		srcNewState.access = (uint64)vk::AccessFlagBits2::eAccelerationStructureReadKHR;
		info.srcAccelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
	}
	if (command.typeAS == AccelerationStructure::Type::Blas)
	{
		info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		dstAsView = OptView<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.dstAS)));
	}
	else
	{
		info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
		dstAsView = OptView<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.dstAS)));
	}
	dstNewState.access |= (uint64)vk::AccessFlagBits2::eAccelerationStructureWriteKHR;

	auto srcOldAsState = command.srcAS ? &AccelerationStructureExt::getBarrierState(**srcAsView) : &tmpState;
	auto& dstOldAsState = AccelerationStructureExt::getBarrierState(**dstAsView);
	addMemoryBarrier(vulkanAPI, *srcOldAsState, dstOldAsState, srcNewState, dstNewState);

	dstNewState.access = (uint64)(vk::AccessFlagBits2::eAccelerationStructureReadKHR |
		vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
	addBufferBarrier(vulkanAPI, dstNewState, command.scratchBuffer); // TODO: synchronize only used part of the scratch buffer.

	auto buildData = (uint8*)AccelerationStructureExt::getBuildData(**dstAsView);
	auto buildDataHeader = ((AccelerationStructure::BuildDataHeader*)buildData);
	auto buildDataOffset = sizeof(AccelerationStructure::BuildDataHeader);
	auto syncBuffers = (ID<Buffer>*)(buildData + buildDataOffset);
	buildDataOffset += buildDataHeader->bufferCount * sizeof(ID<Buffer>);
	auto asArray = (const vk::AccelerationStructureGeometryKHR*)(buildData + buildDataOffset);
	buildDataOffset += buildDataHeader->geometryCount * sizeof(vk::AccelerationStructureGeometryKHR);
	auto rangeInfos = (const vk::AccelerationStructureBuildRangeInfoKHR*)(buildData + buildDataOffset);

	info.flags = toVkBuildFlagsAS(dstAsView->getFlags());
	info.mode = command.isUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate :
		vk::BuildAccelerationStructureModeKHR::eBuild;
	info.dstAccelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**dstAsView);
	info.scratchData = alignSize(scratchBufferView->getDeviceAddress(), 
		(uint64)vulkanAPI->asProperties.minAccelerationStructureScratchOffsetAlignment);
	info.geometryCount = buildDataHeader->geometryCount;
	info.pGeometries = asArray;

	auto bufferCount = buildDataHeader->bufferCount;
	dstNewState.access = (uint64)vk::AccessFlagBits2::eShaderRead;
	for (uint32 i = 0; i < bufferCount; i++)
		addBufferBarrier(vulkanAPI, dstNewState, syncBuffers[i]);

	vulkanAPI->asGeometryInfos.push_back(info);
	vulkanAPI->asRangeInfos.push_back(rangeInfos);
	vulkanAPI->asBuildData.push_back(buildData);

	if (hasAnyFlag(dstAsView->getFlags(), BuildFlagsAS::AllowCompaction))
	{
		buildDataHeader->queryPoolIndex = UINT32_MAX;
		vulkanAPI->asWriteProperties.push_back(info.dstAccelerationStructure);
	}
	else
	{
		buildDataHeader->queryPoolIndex = 0;
		AccelerationStructureExt::getBuildData(**dstAsView) = nullptr;
	}
	
	auto dataIter = this->dataIter + sizeof(BuildAccelerationStructureCommand);

	auto shouldBuild = false;
	if (dataIter < this->dataEnd)
	{
		auto subCommand = (const Command*)dataIter;
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);
		GARDEN_ASSERT(subCommand->type != Command::Type::Unknown);

		if (subCommand->type != Command::Type::BuildAccelerationStructure)
			shouldBuild = true;

		auto subCommandAS = (const BuildAccelerationStructureCommand*)subCommand;
		if (command.typeAS != subCommandAS->typeAS) // Note: We can't mix BLAS and TLAS in one command.
			shouldBuild = true;
	}
	else shouldBuild = true;

	if (shouldBuild)
	{
		processPipelineBarriers();

		instance.buildAccelerationStructuresKHR(vulkanAPI->asGeometryInfos.size(), 
			vulkanAPI->asGeometryInfos.data(), vulkanAPI->asRangeInfos.data());

		if (!vulkanAPI->asWriteProperties.empty())
		{
			vk::QueryPoolCreateInfo createInfo;
			createInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
			createInfo.queryCount = (uint32)vulkanAPI->asWriteProperties.size(); 
			auto queryPool = (vk::QueryPool)vulkanAPI->device.createQueryPool(createInfo);

			auto compactData = new AccelerationStructure::CompactData();
			compactData->queryResults.resize(createInfo.queryCount);
			compactData->queryPool = queryPool;
			compactData->queryPoolRef = createInfo.queryCount - 1;

			dstNewState.access = (uint64)vk::AccessFlagBits2::eAccelerationStructureReadKHR;
			auto& oldAsState = AccelerationStructureExt::getBarrierState(**dstAsView);
			addMemoryBarrier(vulkanAPI, oldAsState, dstNewState);
			processPipelineBarriers();

			instance.resetQueryPool(queryPool, 0, createInfo.queryCount);
			instance.writeAccelerationStructuresPropertiesKHR(vulkanAPI->asWriteProperties, 
				vk::QueryType::eAccelerationStructureCompactedSizeKHR, queryPool, 0);

			uint32 queryPoolIndex = 0;
			for (auto buildData : vulkanAPI->asBuildData)
			{
				if (buildDataHeader->queryPoolIndex == UINT32_MAX)
				{
					buildDataHeader->compactData = compactData;
					buildDataHeader->queryPoolIndex = queryPoolIndex++;
				}
				else free(buildData);
			}
			vulkanAPI->asWriteProperties.clear();
		}
		else
		{
			for (auto buildData : vulkanAPI->asBuildData)
				free(buildData);
		}

		vulkanAPI->asBuildData.clear();
		vulkanAPI->asGeometryInfos.clear();
		vulkanAPI->asRangeInfos.clear();
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyAccelerationStructureCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyAccelerationStructure Command Process");

	vk::CopyAccelerationStructureInfoKHR copyInfo;
	copyInfo.mode = command.isCompact ? vk::CopyAccelerationStructureModeKHR::eCompact :
		vk::CopyAccelerationStructureModeKHR::eClone; // TODO: Serialization mode if needed.
	OptView<AccelerationStructure> srcAsView = {}, dstAsView = {};

	if (command.typeAS == AccelerationStructure::Type::Blas)
	{
		srcAsView = OptView<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.srcAS)));
		copyInfo.src = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
		dstAsView = OptView<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.dstAS)));
		copyInfo.dst = (VkAccelerationStructureKHR)ResourceExt::getInstance(**dstAsView);
	}
	else
	{
		srcAsView = OptView<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.srcAS)));
		copyInfo.src = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
		dstAsView = OptView<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.dstAS)));
		copyInfo.dst = (VkAccelerationStructureKHR)ResourceExt::getInstance(**dstAsView);
	}

	Buffer::BarrierState srcNewState, dstNewState;
	srcNewState.stage = dstNewState.stage = (uint64)vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
	srcNewState.access = (uint64)vk::AccessFlagBits2::eAccelerationStructureReadKHR;
	dstNewState.access = (uint64)vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
	auto& srcOldState = AccelerationStructureExt::getBarrierState(**srcAsView);
	auto& dstOldState = AccelerationStructureExt::getBarrierState(**dstAsView);
	addMemoryBarrier(vulkanAPI, srcOldState, dstOldState, srcNewState, dstNewState);
	processPipelineBarriers();

	instance.copyAccelerationStructureKHR(copyInfo);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const TraceRaysCommand& command)
{
	SET_CPU_ZONE_SCOPED("TraceRays Command Process");

	Buffer::BarrierState newSrcBufferState;
	newSrcBufferState.access = (uint64)vk::AccessFlagBits2::eShaderRead;
	newSrcBufferState.stage = (uint64)vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
	addBufferBarrier(vulkanAPI, newSrcBufferState, command.sbtBuffer);

	auto dataIter = this->dataIter - command.lastSize;
	while (dataIter != this->data)
	{
		auto subCommand = (const Command*)dataIter;
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);
		GARDEN_ASSERT(subCommand->type != Command::Type::Unknown);

		auto commandType = subCommand->type;
		if (isMajorCommand(commandType))
			break;
		dataIter -= subCommand->lastSize;

		if (commandType != Command::Type::BindDescriptorSets)
			continue;

		auto bindDescriptorSetsCommand = (const BindDescriptorSetsCommand*)subCommand;
		auto descriptorSetRange = (const DescriptorSet::Range*)(
			(const uint8*)subCommand + sizeof(BindDescriptorSetsCommandBase));
		addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand->rangeCount);
	}
	processPipelineBarriers();

	vk::StridedDeviceAddressRegionKHR rayGenRegion(command.sbtRegions.rayGenRegion.deviceAddress,
		command.sbtRegions.rayGenRegion.stride, command.sbtRegions.rayGenRegion.size);
	vk::StridedDeviceAddressRegionKHR missRegion(command.sbtRegions.missRegion.deviceAddress,
		command.sbtRegions.missRegion.stride, command.sbtRegions.missRegion.size);
	vk::StridedDeviceAddressRegionKHR hitRegion(command.sbtRegions.hitRegion.deviceAddress,
		command.sbtRegions.hitRegion.stride, command.sbtRegions.hitRegion.size);
	vk::StridedDeviceAddressRegionKHR callRegion(command.sbtRegions.callRegion.deviceAddress,
		command.sbtRegions.callRegion.stride, command.sbtRegions.callRegion.size);

	instance.traceRaysKHR(&rayGenRegion, &missRegion, &hitRegion, &callRegion, 
		command.groupCount.x, command.groupCount.y, command.groupCount.z);
}

void VulkanCommandBuffer::processCommand(const CustomRenderCommand& command)
{
	command.onCommand(this, command.argument);
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BeginLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginLabel Command Process");

	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	array<float, 4> values; *(float4*)values.data() = (float4)command.color;
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	instance.beginDebugUtilsLabelEXT(debugLabel);
}
void VulkanCommandBuffer::processCommand(const EndLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("EndLabel Command Process");
	instance.endDebugUtilsLabelEXT();
}
void VulkanCommandBuffer::processCommand(const InsertLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("InsertLabel Command Process");

	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	array<float, 4> values; *(float4*)values.data() = (float4)command.color;
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	instance.insertDebugUtilsLabelEXT(debugLabel);
}
#endif