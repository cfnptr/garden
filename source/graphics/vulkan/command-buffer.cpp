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

#include "garden/graphics/vulkan/command-buffer.hpp"
#include "garden/graphics/vulkan/api.hpp"
#include "garden/profiler.hpp"

using namespace math;
using namespace garden;

//**********************************************************************************************************************
static vk::CommandBuffer createVkCommandBuffer(vk::Device device, vk::CommandPool commandPool)
{
	vk::CommandBufferAllocateInfo commandBufferInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::CommandBuffer commandBuffer;
	auto allocateResult = device.allocateCommandBuffers(&commandBufferInfo, &commandBuffer);
	vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
	return commandBuffer;
}

//**********************************************************************************************************************
VulkanCommandBuffer::VulkanCommandBuffer(VulkanAPI* vulkanAPI, CommandBufferType type) : CommandBuffer(type)
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
	if (vulkanAPI->hasDebugUtils)
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
	VulkanAPI::get()->device.destroyFence(fence);
}

//**********************************************************************************************************************
static void addMemoryBarrier(VulkanAPI* vulkanAPI, vk::AccessFlags srcAccess, vk::AccessFlags dstAccess)
{
	vk::MemoryBarrier memoryBarrier(srcAccess, dstAccess);
	vulkanAPI->memoryBarriers.push_back(memoryBarrier);
}
static void addMemoryBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& oldState, Buffer::BarrierState newState)
{
	if (VulkanCommandBuffer::isDifferentState(oldState))
	{
		addMemoryBarrier(vulkanAPI, vk::AccessFlags(oldState.access), vk::AccessFlags(newState.access));
		vulkanAPI->oldPipelineStage |= oldState.stage;
		vulkanAPI->newPipelineStage |= newState.stage;
	}
	oldState = newState;
}
static void addMemoryBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& srcOldState, 
	Buffer::BarrierState& dstOldState, Buffer::BarrierState srcNewState, Buffer::BarrierState dstNewState)
{
	if (VulkanCommandBuffer::isDifferentState(srcOldState) || VulkanCommandBuffer::isDifferentState(dstOldState))
	{
		addMemoryBarrier(vulkanAPI, vk::AccessFlags(srcOldState.access | dstOldState.access), 
			vk::AccessFlags(srcNewState.access | dstNewState.access));
		vulkanAPI->oldPipelineStage |= srcOldState.stage | dstOldState.stage;
		vulkanAPI->newPipelineStage |= srcNewState.stage | dstNewState.stage;
	}

	srcOldState = srcNewState;
	dstOldState = dstNewState;
}

//**********************************************************************************************************************
static void addImageBarrier(VulkanAPI* vulkanAPI, const Image::BarrierState& oldImageState, 
	const Image::BarrierState& newImageState, vk::Image image, uint32 baseMip, uint32 mipCount, 
	uint32 baseLayer, uint32 layerCount, vk::ImageAspectFlags aspectFlags)
{
	vk::ImageMemoryBarrier imageMemoryBarrier(
		vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
		vk::ImageLayout(oldImageState.layout), vk::ImageLayout(newImageState.layout),
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, vk::ImageSubresourceRange(
			aspectFlags, baseMip, mipCount, baseLayer, layerCount));
	vulkanAPI->imageMemoryBarriers.push_back(imageMemoryBarrier);
}
static void addImageBarrier(VulkanAPI* vulkanAPI, Image::BarrierState newImageState, ID<ImageView> imageView, 
	vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor)
{
	auto view = vulkanAPI->imageViewPool.get(imageView);
	auto image = vulkanAPI->imagePool.get(view->getImage());
	auto& oldImageState = vulkanAPI->getImageState(
		view->getImage(), view->getBaseMip(), view->getBaseLayer());

	if (VulkanCommandBuffer::isDifferentState(oldImageState, newImageState))
	{
		addImageBarrier(vulkanAPI, oldImageState, newImageState, (VkImage)ResourceExt::getInstance(
			**image), view->getBaseMip(), 1, view->getBaseLayer(), 1, aspectFlags);
		vulkanAPI->oldPipelineStage |= oldImageState.stage;
		vulkanAPI->newPipelineStage |= newImageState.stage;
	}
	VulkanCommandBuffer::updateLayoutTrans(oldImageState, newImageState);
	oldImageState = newImageState;
	ImageExt::isFullBarrier(**image) = image->getMipCount() == 1 && image->getLayerCount() == 1;
}

static void addImageBarriers(VulkanAPI* vulkanAPI, Image::BarrierState newImageState, 
	ID<Image> image, uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount)
{
	auto imageView = vulkanAPI->imagePool.get(image);
	auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
	auto aspectFlags = toVkImageAspectFlags(imageView->getFormat());

	auto newFullBarrier = baseMip == 0 && mipCount == imageView->getMipCount() &&
		baseLayer == 0 && layerCount == imageView->getLayerCount();
	if (ImageExt::isFullBarrier(**imageView) && newFullBarrier)
	{
		const auto& oldImageState = vulkanAPI->getImageState(image, 0, 0);
		if (VulkanCommandBuffer::isDifferentState(oldImageState, newImageState))
		{
			addImageBarrier(vulkanAPI, oldImageState, newImageState, 
				vkImage, 0, mipCount, 0, layerCount, aspectFlags);
			vulkanAPI->oldPipelineStage |= oldImageState.stage;
			vulkanAPI->newPipelineStage |= newImageState.stage;
		}
		VulkanCommandBuffer::updateLayoutTrans(oldImageState, newImageState);

		auto& barrierStates = ImageExt::getBarrierStates(**imageView);
		for (auto& oldBarrierState : barrierStates)
			oldBarrierState = newImageState;
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
					vulkanAPI->oldPipelineStage |= oldImageState.stage;
					vulkanAPI->newPipelineStage |= newImageState.stage;
				}
				VulkanCommandBuffer::updateLayoutTrans(oldImageState, newImageState);
				oldImageState = newImageState;
			}
		}
	}
	ImageExt::isFullBarrier(**imageView) = newFullBarrier;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addBufferBarrier(VulkanAPI* vulkanAPI, 
	Buffer::BarrierState newBufferState, ID<Buffer> buffer, uint64 size, uint64 offset)
{
	// TODO: we can specify only required buffer range, not full range.
	auto& oldBufferState = vulkanAPI->getBufferState(buffer);
	if (isDifferentState(oldBufferState))
	{
		auto bufferView = vulkanAPI->bufferPool.get(buffer);
		vk::BufferMemoryBarrier bufferMemoryBarrier(
			vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 
			(VkBuffer)ResourceExt::getInstance(**bufferView), offset, size);
		vulkanAPI->bufferMemoryBarriers.push_back(bufferMemoryBarrier);
		vulkanAPI->oldPipelineStage |= oldBufferState.stage;
		vulkanAPI->newPipelineStage |= newBufferState.stage;
	}
	oldBufferState = newBufferState;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addDescriptorSetBarriers(VulkanAPI* vulkanAPI, 
	const DescriptorSet::Range* descriptorSetRange, uint32 rangeCount)
{
	SET_CPU_ZONE_SCOPED("Descriptor Set Barriers Add");
	// also add to the shaders noncoherent tag to skip sync if different buffer or image parts.

	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptor.set);
		const auto& dsUniforms = descriptorSet->getUniforms();
		auto pipelineView = vulkanAPI->getPipelineView(
			descriptorSet->getPipelineType(), descriptorSet->getPipeline());
		const auto& pipelineUniforms = pipelineView->getUniforms();

		for (const auto& pipelineUniform : pipelineUniforms)
		{
			auto uniform = pipelineUniform.second;
			if (uniform.descriptorSetIndex != i)
				continue;

			auto setCount = descriptor.offset + descriptor.count;
			const auto& dsUniform = dsUniforms.at(pipelineUniform.first);

			if (isSamplerType(uniform.type) || isImageType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Image/Sampler Barriers Process");

				Image::BarrierState newImageState;
				newImageState.stage = (uint32)toVkPipelineStages(uniform.shaderStages);

				if (isSamplerType(uniform.type))
				{
					newImageState.access = (uint32)vk::AccessFlagBits::eShaderRead;
					newImageState.layout = (uint32)vk::ImageLayout::eShaderReadOnlyOptimal;
				}
				else
				{
					if (uniform.readAccess)
						newImageState.access |= (uint32)vk::AccessFlagBits::eShaderRead;
					if (uniform.writeAccess)
						newImageState.access |= (uint32)vk::AccessFlagBits::eShaderWrite;
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
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), 
								imageView->getBaseMip(), imageView->getMipCount(), 
								imageView->getBaseLayer(), imageView->getLayerCount());
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
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), 
								imageView->getBaseMip(), imageView->getMipCount(), 
								imageView->getBaseLayer(), imageView->getLayerCount());
						}
					}
				}
			}
			else if (isBufferType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Buffer Barriers Process");

				Buffer::BarrierState newBufferState;
				newBufferState.stage = (uint32)toVkPipelineStages(uniform.shaderStages);

				if (uniform.type == GslUniformType::UniformBuffer)
				{
					newBufferState.access = (uint32)vk::AccessFlagBits::eUniformRead;
				}
				else
				{
					if (uniform.readAccess)
						newBufferState.access |= (uint32)vk::AccessFlagBits::eShaderRead;
					if (uniform.writeAccess)
						newBufferState.access |= (uint32)vk::AccessFlagBits::eShaderWrite;
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
				newAsState.access = (uint32)vk::AccessFlagBits::eAccelerationStructureReadKHR;
				newAsState.stage = (uint32)vk::PipelineStageFlagBits::eRayTracingShaderKHR;

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
void VulkanCommandBuffer::processPipelineBarriers(VulkanAPI* vulkanAPI)
{
	SET_CPU_ZONE_SCOPED("Pipeline Barriers Process");

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

	// TODO: combine overlapping barriers.
	// TODO: use dependency flags

	instance.pipelineBarrier(oldPipelineStage, newPipelineStage, {}, 
		(uint32)vulkanAPI->memoryBarriers.size(), vulkanAPI->memoryBarriers.data(),
		(uint32)vulkanAPI->bufferMemoryBarriers.size(), vulkanAPI->bufferMemoryBarriers.data(),
		(uint32)vulkanAPI->imageMemoryBarriers.size(), vulkanAPI->imageMemoryBarriers.data());

	vulkanAPI->memoryBarriers.clear();
	vulkanAPI->imageMemoryBarriers.clear();
	vulkanAPI->bufferMemoryBarriers.clear();

	vulkanAPI->oldPipelineStage = 0;
	vulkanAPI->newPipelineStage = 0;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::submit()
{
	SET_CPU_ZONE_SCOPED("Command Buffer Submit");

	auto vulkanAPI = VulkanAPI::get();
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
		instance.resetQueryPool(inFlightFrame.queryPool, 0, 2);
		instance.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, inFlightFrame.queryPool, 0);
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
			Image::BarrierState newImageState;
			newImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
			newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
			newImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

			vk::ImageMemoryBarrier imageMemoryBarrier(
				vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				vkImage, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			instance.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
				vk::PipelineStageFlags(newImageState.stage), {}, {}, {}, imageMemoryBarrier);
			oldImageState = newImageState;

			array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
			instance.clearColorImage(vkImage, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(clearColor),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		}

		Image::BarrierState newImageState;
		newImageState.access = (uint32)vk::AccessFlagBits::eNone;
		newImageState.layout = (uint32)vk::ImageLayout::ePresentSrcKHR;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eBottomOfPipe;
		vk::ImageMemoryBarrier imageMemoryBarrier(
			vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
			(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			vkImage, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		instance.pipelineBarrier(oldImageState.stage == (uint32)vk::PipelineStageFlagBits::eNone ?
			vk::PipelineStageFlagBits::eTopOfPipe : (vk::PipelineStageFlagBits)oldImageState.stage,
			vk::PipelineStageFlags(newImageState.stage), {}, {}, {}, imageMemoryBarrier);
		oldImageState = newImageState;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (vulkanAPI->recordGpuTime)
		{
			auto& inFlightFrame = swapchain->getInFlightFrame();
			instance.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, inFlightFrame.queryPool, 1);
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
	return VulkanAPI::get()->device.getFenceStatus(fence) == vk::Result::eNotReady;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addRenderPassBarriers(VulkanAPI* vulkanAPI, psize commandOffset)
{
	SET_CPU_ZONE_SCOPED("Render Pass Barriers Add");

	while (commandOffset < size)
	{
		auto subCommand = (const Command*)(data + commandOffset);
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);

		auto commandType = subCommand->type;
		if (commandType == Command::Type::EndRenderPass)
			break;

		commandOffset += subCommand->thisSize;

		if (commandType == Command::Type::BindDescriptorSets)
		{
			const auto& bindDescriptorSetsCommand = *(const BindDescriptorSetsCommand*)subCommand;
			auto descriptorSetRange = (const DescriptorSet::Range*)(
				(const uint8*)subCommand + sizeof(BindDescriptorSetsCommandBase));
			addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand.rangeCount);
		}
		else if (commandType == Command::Type::Draw)
		{
			const auto& drawCommand = *(const DrawCommand*)subCommand;
			if (!drawCommand.vertexBuffer)
				continue;

			Buffer::BarrierState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			addBufferBarrier(vulkanAPI, newBufferState, drawCommand.vertexBuffer);
		}
		else if (commandType == Command::Type::DrawIndexed)
		{
			const auto& drawIndexedCommand = *(const DrawIndexedCommand*)subCommand;
			
			Buffer::BarrierState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand.vertexBuffer);

			newBufferState.access |= (uint32)vk::AccessFlagBits::eIndexRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand.indexBuffer);
		}
	}
}

//**********************************************************************************************************************
static bool findLastSubpassInput(const vector<Framebuffer::Subpass>& subpasses,
	ID<ImageView> colorAttachment, ShaderStage& shaderStages) noexcept
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
				shaderStages = inputAttachment.shaderStages;
				isLastInput = true;
				break;
			}
		}
	}
	return isLastInput;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BufferBarrierCommand& command)
{
	SET_CPU_ZONE_SCOPED("BufferBarrier Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto commandBufferData = (const uint8*)&command;
	auto buffers = (const ID<Buffer>*)(commandBufferData + sizeof(BufferBarrierCommandBase));

	for (uint32 i = 0; i < command.bufferCount; i++)
		addBufferBarrier(vulkanAPI, command.newState, buffers[i]);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BeginRenderPassCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginRenderPass Command Process");

	auto vulkanAPI = VulkanAPI::get();
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

		Image::BarrierState newImageState;
		newImageState.access = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
		if (colorAttachment.flags.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eColorAttachmentRead;
		newImageState.layout = (uint32)vk::ImageLayout::eColorAttachmentOptimal;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
		addImageBarrier(vulkanAPI, newImageState, colorAttachment.imageView);
		
		if (noSubpass)
		{
			vk::AttachmentLoadOp loadOperation;
			vk::ClearValue clearValue;

			if (colorAttachment.flags.clear)
			{
				array<float, 4> color;
				*(float4*)color.data() = clearColors[i];
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
			array<float, 4> color;
			*(float4*)color.data() = clearColors[i];
			vulkanAPI->clearValues.emplace_back(vk::ClearColorValue(color));
		}
	}
	
	vk::RenderingAttachmentInfoKHR depthStencilAttachmentInfo;
	vk::RenderingAttachmentInfoKHR* depthAttachmentInfoPtr = nullptr;
	vk::RenderingAttachmentInfoKHR* stencilAttachmentInfoPtr = nullptr;

	if (framebuffer->getDepthStencilAttachment().imageView)
	{
		auto depthStencilAttachment = framebuffer->getDepthStencilAttachment();
		
		Image::BarrierState newImageState;
		if (depthStencilAttachment.flags.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentRead;
		if (depthStencilAttachment.flags.clear || depthStencilAttachment.flags.load)
			newImageState.stage |= (uint32)vk::PipelineStageFlagBits::eEarlyFragmentTests;
		if (depthStencilAttachment.flags.store)
		{
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			newImageState.stage |= (uint32)vk::PipelineStageFlagBits::eLateFragmentTests;
		}

		auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
		auto imageFormat = imageView->getFormat();
		vk::ImageAspectFlags aspectFlags;

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
			aspectFlags = vk::ImageAspectFlagBits::eDepth;
		}
		else if (isFormatStencilOnly(imageFormat))
		{
			if (noSubpass)
			{
				depthStencilAttachmentInfo.imageLayout = vk::ImageLayout::eStencilAttachmentOptimal;
				stencilAttachmentInfoPtr = &depthStencilAttachmentInfo;
			}
			newImageState.layout = (uint32)vk::ImageLayout::eStencilAttachmentOptimal;
			aspectFlags = vk::ImageAspectFlagBits::eStencil;
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
			aspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		}

		addImageBarrier(vulkanAPI, newImageState, depthStencilAttachment.imageView, aspectFlags);
	}

	auto commandOffset = (commandBufferData - data) + command.thisSize;
	addRenderPassBarriers(vulkanAPI, commandOffset);
	processPipelineBarriers(vulkanAPI);

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
		else 
			instance.beginRendering(renderingInfo);
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

			ShaderStage shaderStages;
			auto isLastInput = findLastSubpassInput(subpasses, colorAttachment.imageView, shaderStages);

			if (isLastInput)
			{
				imageState.access = (uint32)vk::AccessFlagBits::eShaderRead;
				imageState.stage = (uint32)toVkPipelineStages(shaderStages);
			}
			else
			{
				imageState.access = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
				imageState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
			}
		}

		if (framebuffer->getDepthStencilAttachment().imageView)
		{
			auto depthStencilAttachment = framebuffer->getDepthStencilAttachment();
			auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
			auto& imageState = vulkanAPI->getImageState(imageView->getImage(),
				imageView->getBaseMip(), imageView->getBaseLayer());
			imageState.layout = (uint32)vk::ImageLayout::eGeneral;

			ShaderStage shaderStages;
			auto isLastInput = findLastSubpassInput(subpasses, depthStencilAttachment.imageView, shaderStages);

			if (isLastInput)
			{
				imageState.access = (uint32)vk::AccessFlagBits::eShaderRead;
				imageState.stage = (uint32)toVkPipelineStages(shaderStages);
			}
			else
			{
				imageState.access = (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				imageState.stage = (uint32)vk::PipelineStageFlagBits::eEarlyFragmentTests;
			}
		}
	}

	if (!command.asyncRecording)
	{
		vulkanAPI->currentPipelines[0] = {};
		vulkanAPI->currentPipelineTypes[0] = {}; 
		vulkanAPI->currentPipelineVariants[0] = 0;
		vulkanAPI->currentVertexBuffers[0] = {};
		vulkanAPI->currentIndexBuffers[0] = {};
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const NextSubpassCommand& command)
{
	SET_CPU_ZONE_SCOPED("NextSubpass Command Process");

	instance.nextSubpass(command.asyncRecording ?
		vk::SubpassContents::eSecondaryCommandBuffers : vk::SubpassContents::eInline);

	if (!command.asyncRecording)
	{
		auto vulkanAPI = VulkanAPI::get();
		vulkanAPI->currentPipelines[0] = {};
		vulkanAPI->currentPipelineTypes[0] = {};
		vulkanAPI->currentPipelineVariants[0] = 0;
		vulkanAPI->currentVertexBuffers[0] = {};
		vulkanAPI->currentIndexBuffers[0] = {};
	}
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
		if (VulkanAPI::get()->versionMinor < 3)
			instance.endRenderingKHR();
		else
			instance.endRendering();
	}
	else
	{
		instance.endRenderPass();
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const ClearAttachmentsCommand& command)
{
	SET_CPU_ZONE_SCOPED("ClearAttachments Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto framebufferView = vulkanAPI->framebufferPool.get(command.framebuffer);
	const auto& colorAttachments = framebufferView->getColorAttachments();

	auto attachments = (const Framebuffer::ClearAttachment*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase));
	auto regions = (const Framebuffer::ClearRegion*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase) +
		command.attachmentCount * sizeof(Framebuffer::ClearAttachment));

	if (vulkanAPI->clearAttachments.size() < command.attachmentCount)
		vulkanAPI->clearAttachments.resize(command.attachmentCount);
	if (vulkanAPI->clearAttachmentsRects.size() < command.regionCount)
		vulkanAPI->clearAttachmentsRects.resize(command.regionCount);

	for (uint8 i = 0; i < command.attachmentCount; i++)
	{
		auto attachment = attachments[i];
		ID<ImageView> attachmentView;

		if (attachment.index < colorAttachments.size())
		{
			attachmentView = colorAttachments[attachment.index].imageView;
		}
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
		if (isFormatFloat(format) || isFormatSrgb(format) || isFormatNorm(format))
		{
			memcpy(clearValue.color.float32.data(), &attachment.clearColor.floatValue, sizeof(float) * 4);
		}
		else if (isFormatSint(format))
		{
			memcpy(clearValue.color.int32.data(), &attachment.clearColor.intValue, sizeof(int32) * 4);
		}
		else if (isFormatUint(format))
		{
			memcpy(clearValue.color.uint32.data(), &attachment.clearColor.uintValue, sizeof(uint32) * 4);
		}
		else
		{
			clearValue.depthStencil.depth = attachment.clearColor.deptStencilValue.depth;
			clearValue.depthStencil.stencil = attachment.clearColor.deptStencilValue.stencil;
		}

		vulkanAPI->clearAttachments[i] = vk::ClearAttachment(
			toVkImageAspectFlags(format), attachment.index, clearValue);
	}

	for (uint32 i = 0; i < command.regionCount; i++)
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
	instance.clearAttachments(command.attachmentCount, vulkanAPI->clearAttachments.data(),
		command.regionCount, vulkanAPI->clearAttachmentsRects.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BindPipelineCommand& command)
{	
	SET_CPU_ZONE_SCOPED("BindPipeline Command Process");

	auto vulkanAPI = VulkanAPI::get();
	if (command.pipeline != vulkanAPI->currentPipelines[0] || 
		command.pipelineType != vulkanAPI->currentPipelineTypes[0] ||
		command.variant != vulkanAPI->currentPipelineVariants[0])
	{
		auto pipelineView = vulkanAPI->getPipelineView(command.pipelineType, command.pipeline);
		auto pipeline = ResourceExt::getInstance(**pipelineView);
		instance.bindPipeline(toVkPipelineBindPoint(command.pipelineType), 
			pipelineView->getVariantCount() > 1 ? ((VkPipeline*)pipeline)[command.variant] : (VkPipeline)pipeline);

		vulkanAPI->currentPipelines[0] = command.pipeline;
		vulkanAPI->currentPipelineTypes[0] = command.pipelineType;
		vulkanAPI->currentPipelineVariants[0] = command.variant;
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BindDescriptorSetsCommand& command)
{
	SET_CPU_ZONE_SCOPED("BindDescriptorSets Command Process");

	if (command.asyncRecording)
		return;

	auto vulkanAPI = VulkanAPI::get();
	auto descriptorSetRange = (const DescriptorSet::Range*)(
		(const uint8*)&command + sizeof(BindDescriptorSetsCommandBase));
	auto& descriptorSets = vulkanAPI->bindDescriptorSets[0];
	// TODO: maybe detect already bound descriptor sets?

	for (uint8 i = 0; i < command.rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptor.set);
		auto instance = (vk::DescriptorSet*)ResourceExt::getInstance(**descriptorSet);

		if (descriptorSet->getSetCount() > 1)
		{
			auto count = descriptor.offset + descriptor.count;
			for (uint32 j = descriptor.offset; j < count; j++)
				descriptorSets.push_back(instance[j]);
		}
		else
		{
			descriptorSets.push_back((VkDescriptorSet)instance);
		}
	}

	auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptorSetRange[0].set);
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

	instance.pushConstants((VkPipelineLayout)command.pipelineLayout, (vk::ShaderStageFlags)command.shaderStages,
		0, command.dataSize, (const uint8*)&command + sizeof(PushConstantsCommandBase));
}

void VulkanCommandBuffer::processCommand(const SetViewportCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetViewport Command Process");

	vk::Viewport viewport(command.viewport.x, command.viewport.y,
		command.viewport.z, command.viewport.w, 0.0f, 1.0f); // TODO: depth
	instance.setViewport(0, 1, &viewport); // TODO: multiple viewports
}

void VulkanCommandBuffer::processCommand(const SetScissorCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetScissor Command Process");

	vk::Rect2D scissor({ command.scissor.x, command.scissor.y },
		{ (uint32)command.scissor.z, (uint32)command.scissor.w });
	instance.setScissor(0, 1, &scissor); // TODO: multiple scissors
}

void VulkanCommandBuffer::processCommand(const SetViewportScissorCommand& command)
{
	SET_CPU_ZONE_SCOPED("SetViewportScissor Command Process");

	vk::Viewport viewport(command.viewportScissor.x, command.viewportScissor.y,
		command.viewportScissor.z, command.viewportScissor.w, 0.0f, 1.0f);
	vk::Rect2D scissor(
		{ (int32)command.viewportScissor.x, (int32)command.viewportScissor.y },
		{ (uint32)command.viewportScissor.z, (uint32)command.viewportScissor.w });
	instance.setViewport(0, 1, &viewport); // TODO: multiple viewports
	instance.setScissor(0, 1, &scissor);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DrawCommand& command)
{
	SET_CPU_ZONE_SCOPED("Draw Command Process");

	if (command.asyncRecording)
		return;

	auto vulkanAPI = VulkanAPI::get();
	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	if (command.vertexBuffer && command.vertexBuffer != vulkanAPI->currentVertexBuffers[0])
	{
		const vk::DeviceSize size = 0;
		auto buffer = vulkanAPI->bufferPool.get(command.vertexBuffer);
		vk::Buffer vkBuffer = (VkBuffer)ResourceExt::getInstance(**buffer);
		instance.bindVertexBuffers(0, 1, &vkBuffer, &size);
		vulkanAPI->currentVertexBuffers[0] = command.vertexBuffer;
	}

	instance.draw(command.vertexCount, command.instanceCount, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DrawIndexedCommand& command)
{
	SET_CPU_ZONE_SCOPED("DrawIndexed Command Process");

	if (command.asyncRecording)
		return;

	auto vulkanAPI = VulkanAPI::get();
	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	if (command.vertexBuffer != vulkanAPI->currentVertexBuffers[0])
	{
		const vk::DeviceSize size = 0;
		auto buffer = vulkanAPI->bufferPool.get(command.vertexBuffer);
		vk::Buffer vkBuffer = (VkBuffer)ResourceExt::getInstance(**buffer);
		instance.bindVertexBuffers(0, 1, &vkBuffer, &size);
		vulkanAPI->currentVertexBuffers[0] = command.vertexBuffer;
	}
	if (command.indexBuffer != vulkanAPI->currentIndexBuffers[0])
	{
		auto buffer = vulkanAPI->bufferPool.get(command.indexBuffer);
		instance.bindIndexBuffer((VkBuffer)ResourceExt::getInstance(**buffer),
			(vk::DeviceSize)(command.indexOffset * toBinarySize(command.indexType)),
			toVkIndexType(command.indexType));
		vulkanAPI->currentIndexBuffers[0] = command.indexBuffer;
	}

	instance.drawIndexed(command.indexCount, command.instanceCount,
		command.indexOffset, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const DispatchCommand& command)
{
	SET_CPU_ZONE_SCOPED("Dispatch Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto commandBufferData = (const uint8*)&command;
	auto commandOffset = (int64)(commandBufferData - data) - (int64)command.lastSize;

	while (commandOffset >= 0)
	{
		auto subCommand = (const Command*)(data + commandOffset);
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);
		if (subCommand->lastSize == 0)
			break;

		auto commandType = subCommand->type;
		if (commandType == Command::Type::Dispatch | commandType == Command::Type::TraceRays |
			commandType == Command::Type::EndRenderPass)
		{
			break;
		}

		commandOffset -= subCommand->lastSize;
		if (commandType != Command::Type::BindDescriptorSets)
			continue;

		const auto& bindDescriptorSetsCommand = *(const BindDescriptorSetsCommand*)subCommand;
		auto descriptorSetRange = (const DescriptorSet::Range*)(
			(const uint8*)subCommand + sizeof(BindDescriptorSetsCommandBase));
		addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand.rangeCount);
	}

	processPipelineBarriers(vulkanAPI);
	instance.dispatch(command.groupCount.x, command.groupCount.y, command.groupCount.z);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const FillBufferCommand& command)
{
	SET_CPU_ZONE_SCOPED("FillBuffer Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto bufferView = vulkanAPI->bufferPool.get(command.buffer);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**bufferView);

	Buffer::BarrierState newBufferState;
	newBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	addBufferBarrier(vulkanAPI, newBufferState, command.buffer,
		command.size == 0 ? VK_WHOLE_SIZE : command.size, command.offset);
	processPipelineBarriers(vulkanAPI);

	instance.fillBuffer(vkBuffer, command.offset, command.size == 0 ? VK_WHOLE_SIZE : command.size, command.data);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyBufferCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyBuffer Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto srcBuffer = vulkanAPI->bufferPool.get(command.source);
	auto dstBuffer = vulkanAPI->bufferPool.get(command.destination);
	auto vkSrcBuffer = (VkBuffer)ResourceExt::getInstance(**srcBuffer);
	auto vkDstBuffer = (VkBuffer)ResourceExt::getInstance(**dstBuffer);
	auto regions = (const Buffer::CopyRegion*)((const uint8*)&command + sizeof(CopyBufferCommandBase));

	if (vulkanAPI->bufferCopies.size() < command.regionCount)
		vulkanAPI->bufferCopies.resize(command.regionCount);

	Buffer::BarrierState newSrcBufferState;
	newSrcBufferState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcBufferState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	Buffer::BarrierState newDstBufferState;
	newDstBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstBufferState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		vulkanAPI->bufferCopies[i] = vk::BufferCopy(region.srcOffset, region.dstOffset,
			region.size == 0 ? srcBuffer->getBinarySize() : region.size);
		addBufferBarrier(vulkanAPI, newSrcBufferState, command.source);
		addBufferBarrier(vulkanAPI, newDstBufferState, command.destination);
	}

	processPipelineBarriers(vulkanAPI);
	instance.copyBuffer(vkSrcBuffer, vkDstBuffer, command.regionCount, vulkanAPI->bufferCopies.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const ClearImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("ClearImage Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto image = vulkanAPI->imagePool.get(command.image);
	auto vkImage = (VkImage)ResourceExt::getInstance(**image);
	auto regions = (const Image::ClearRegion*)((const uint8*)&command + sizeof(ClearImageCommandBase));
	auto aspectFlags = toVkImageAspectFlags(image->getFormat());

	if (vulkanAPI->imageClears.size() < command.regionCount)
		vulkanAPI->imageClears.resize(command.regionCount);

	Image::BarrierState newImageState;
	newImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		auto mipCount = region.mipCount == 0 ? image->getMipCount() : region.mipCount;
		auto layerCount = region.layerCount == 0 ? image->getLayerCount() : region.layerCount;

		vulkanAPI->imageClears[i] = vk::ImageSubresourceRange(aspectFlags,
			region.baseMip, mipCount, region.baseLayer, layerCount);
		addImageBarriers(vulkanAPI, newImageState, command.image, 
			region.baseMip, mipCount, region.baseLayer, layerCount);
	}

	processPipelineBarriers(vulkanAPI);

	if (isFormatColor(image->getFormat()))
	{
		vk::ClearColorValue clearValue;
		if (command.clearType == 1)
			memcpy(clearValue.float32.data(), &command.color, sizeof(float) * 4);
		else if (command.clearType == 2)
			memcpy(clearValue.int32.data(), &command.color, sizeof(int32) * 4);
		else if (command.clearType == 3)
			memcpy(clearValue.uint32.data(), &command.color, sizeof(uint32) * 4);
		else abort();

		instance.clearColorImage(vkImage, vk::ImageLayout::eTransferDstOptimal,
			&clearValue, command.regionCount, vulkanAPI->imageClears.data());
	}
	else
	{
		vk::ClearDepthStencilValue clearValue;
		clearValue.depth = command.color.x;
		memcpy(&clearValue.stencil, &command.color.y, sizeof(uint32));

		instance.clearDepthStencilImage(vkImage, vk::ImageLayout::eTransferDstOptimal,
			&clearValue, command.regionCount, vulkanAPI->imageClears.data());
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyImage Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto srcImage = vulkanAPI->imagePool.get(command.source);
	auto dstImage = vulkanAPI->imagePool.get(command.destination);
	auto vkSrcImage = (VkImage)ResourceExt::getInstance(**srcImage);
	auto vkDstImage = (VkImage)ResourceExt::getInstance(**dstImage);
	auto regions = (const Image::CopyImageRegion*)((const uint8*)&command + sizeof(CopyImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->getFormat());
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->getFormat());

	if (vulkanAPI->imageCopies.size() < command.regionCount)
		vulkanAPI->imageCopies.resize(command.regionCount);

	Image::BarrierState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	Image::BarrierState newDstImageState;
	newDstImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newDstImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	for (uint32 i = 0; i < command.regionCount; i++)
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
		else
		{
			extent = vk::Extent3D(region.extent.x, region.extent.y, region.extent.z);
		}

		vulkanAPI->imageCopies[i] = vk::ImageCopy(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);

		addImageBarriers(vulkanAPI, newSrcImageState, command.source, 
			region.srcMipLevel, 1, region.srcBaseLayer, srcSubresource.layerCount);
		addImageBarriers(vulkanAPI, newDstImageState, command.destination, 
			region.dstMipLevel, 1, region.dstBaseLayer, dstSubresource.layerCount);
	}

	processPipelineBarriers(vulkanAPI);

	instance.copyImage(vkSrcImage, vk::ImageLayout::eTransferSrcOptimal, vkDstImage, 
		vk::ImageLayout::eTransferDstOptimal, command.regionCount, vulkanAPI->imageCopies.data());
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const CopyBufferImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("CopyBufferImage Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto buffer = vulkanAPI->bufferPool.get(command.buffer);
	auto image = vulkanAPI->imagePool.get(command.image);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**buffer);
	auto vkImage = (VkImage)ResourceExt::getInstance(**image);
	auto regions = (const Image::CopyBufferRegion*)((const uint8*)&command + sizeof(CopyBufferImageCommandBase));
	auto aspectFlags = toVkImageAspectFlags(image->getFormat());

	if (vulkanAPI->bufferImageCopies.size() < command.regionCount)
		vulkanAPI->bufferImageCopies.resize(command.regionCount);

	Buffer::BarrierState newBufferState;
	newBufferState.access = command.toBuffer ?
		(uint32)vk::AccessFlagBits::eTransferWrite : (uint32)vk::AccessFlagBits::eTransferRead;
	newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	Image::BarrierState newImageState;
	newImageState.access = command.toBuffer ?
		(uint32)vk::AccessFlagBits::eTransferRead : (uint32)vk::AccessFlagBits::eTransferWrite;
	newImageState.layout = command.toBuffer ?
		(uint32)vk::ImageLayout::eTransferSrcOptimal : (uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		vk::ImageSubresourceLayers imageSubresource(aspectFlags, region.imageMipLevel, region.imageBaseLayer);
		imageSubresource.layerCount = region.imageLayerCount == 0 ? image->getLayerCount() : region.imageLayerCount;
		vk::Offset3D dstOffset(region.imageOffset.x, region.imageOffset.y, region.imageOffset.z);
		vk::Extent3D dstExtent;
		
		if (region.imageExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(image->getSize(), region.imageMipLevel);
			dstExtent = vk::Extent3D(mipImageSize.getX(), mipImageSize.getY(), 
				image->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else
		{
			dstExtent = vk::Extent3D(region.imageExtent.x, region.imageExtent.y, region.imageExtent.z);
		}

		vulkanAPI->bufferImageCopies[i] = vk::BufferImageCopy((vk::DeviceSize)region.bufferOffset,
			region.bufferRowLength, region.bufferImageHeight, imageSubresource, dstOffset, dstExtent);
		addBufferBarrier(vulkanAPI, newBufferState, command.buffer);

		addImageBarriers(vulkanAPI, newImageState, command.image, region.imageMipLevel, 
			1, region.imageBaseLayer, imageSubresource.layerCount);
	}

	processPipelineBarriers(vulkanAPI);

	if (command.toBuffer)
	{
		instance.copyImageToBuffer(vkImage, vk::ImageLayout::eTransferSrcOptimal,
			vkBuffer, command.regionCount, vulkanAPI->bufferImageCopies.data());
	}
	else
	{
		instance.copyBufferToImage(vkBuffer, vkImage, vk::ImageLayout::eTransferDstOptimal, 
			command.regionCount, vulkanAPI->bufferImageCopies.data());
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BlitImageCommand& command)
{
	SET_CPU_ZONE_SCOPED("BlitImage Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto srcImage = vulkanAPI->imagePool.get(command.source);
	auto dstImage = vulkanAPI->imagePool.get(command.destination);
	auto vkSrcImage = (VkImage)ResourceExt::getInstance(**srcImage);
	auto vkDstImage = (VkImage)ResourceExt::getInstance(**dstImage);
	auto regions = (const Image::BlitRegion*)( (const uint8*)&command + sizeof(BlitImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->getFormat());
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->getFormat());

	if (vulkanAPI->imageBlits.size() < command.regionCount)
		vulkanAPI->imageBlits.resize(command.regionCount);

	Image::BarrierState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	Image::BarrierState newDstImageState;
	newDstImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newDstImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	for (uint32 i = 0; i < command.regionCount; i++)
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
		else
		{
			srcBounds[1] = vk::Offset3D(region.srcExtent.x, region.srcExtent.y, region.srcExtent.z);
		}
		if (region.dstExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(dstImage->getSize(), region.dstMipLevel);
			dstBounds[1] = vk::Offset3D(mipImageSize.getX(), mipImageSize.getY(), 
				dstImage->getType() == Image::Type::Texture3D ? mipImageSize.getZ() : 1);
		}
		else
		{
			dstBounds[1] = vk::Offset3D(region.dstExtent.x, region.dstExtent.y, region.dstExtent.z);
		}

		vulkanAPI->imageBlits[i] = vk::ImageBlit(srcSubresource, srcBounds, dstSubresource, dstBounds);

		addImageBarriers(vulkanAPI, newSrcImageState, command.source, 
			region.srcMipLevel, 1, region.srcBaseLayer, srcSubresource.layerCount);
		addImageBarriers(vulkanAPI, newDstImageState, command.destination, 
			region.dstMipLevel, 1, region.dstBaseLayer, dstSubresource.layerCount);
	}

	processPipelineBarriers(vulkanAPI);

	instance.blitImage(vkSrcImage, vk::ImageLayout::eTransferSrcOptimal,
		vkDstImage, vk::ImageLayout::eTransferDstOptimal, command.regionCount, 
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

	auto vulkanAPI = VulkanAPI::get();
	auto scratchBufferView = vulkanAPI->bufferPool.get(command.scratchBuffer);

	Buffer::BarrierState srcNewState, dstNewState, tmpState;
	srcNewState.stage = dstNewState.stage = (uint32)vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR;

	vk::AccelerationStructureBuildGeometryInfoKHR info;
	View<AccelerationStructure> srcAsView, dstAsView;
	if (command.srcAS)
	{
		if (command.typeAS == AccelerationStructure::Type::Blas)
			srcAsView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.srcAS)));
		else
			srcAsView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.srcAS)));

		srcNewState.access = (uint32)vk::AccessFlagBits::eAccelerationStructureReadKHR;
		info.srcAccelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
	}
	if (command.typeAS == AccelerationStructure::Type::Blas)
	{
		info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		dstAsView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.dstAS)));
	}
	else
	{
		info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
		dstAsView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.dstAS)));
	}
	dstNewState.access |= (uint32)vk::AccessFlagBits::eAccelerationStructureWriteKHR;

	auto srcOldAsState = command.srcAS ? &AccelerationStructureExt::getBarrierState(**srcAsView) : &tmpState;
	auto& dstOldAsState = AccelerationStructureExt::getBarrierState(**dstAsView);
	addMemoryBarrier(vulkanAPI, *srcOldAsState, dstOldAsState, srcNewState, dstNewState);

	dstNewState.access = (uint32)(vk::AccessFlagBits::eAccelerationStructureReadKHR |
		vk::AccessFlagBits::eAccelerationStructureWriteKHR);
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

	dstNewState.access = (uint32)vk::AccessFlagBits::eShaderRead;
	for (uint32 i = 0; i < buildDataHeader->bufferCount; i++)
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
	
	auto commandOffset = (((const uint8*)&command) - data) + command.thisSize;
	auto shouldBuild = false;
	if (commandOffset < size)
	{
		auto subCommand = (const Command*)(data + commandOffset);
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);

		if (subCommand->type != Command::Type::BuildAccelerationStructure)
			shouldBuild = true;

		auto subCommandAS = (const BuildAccelerationStructureCommand*)subCommand;
		if (command.typeAS != subCommandAS->typeAS) // Note: We can't mix BLAS and TLAS in one command.
			shouldBuild = true;
	}
	else
	{
		shouldBuild = true;
	}

	if (shouldBuild)
	{
		processPipelineBarriers(vulkanAPI);

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

			dstNewState.access = (uint32)vk::AccessFlagBits::eAccelerationStructureReadKHR;
			auto& oldAsState = AccelerationStructureExt::getBarrierState(**dstAsView);
			addMemoryBarrier(vulkanAPI, oldAsState, dstNewState);
			processPipelineBarriers(vulkanAPI);

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

	auto vulkanAPI = VulkanAPI::get();
	vk::CopyAccelerationStructureInfoKHR copyInfo;
	copyInfo.mode = command.isCompact ? vk::CopyAccelerationStructureModeKHR::eCompact :
		vk::CopyAccelerationStructureModeKHR::eClone; // TODO: Serialization mode if needed.
	View<AccelerationStructure> srcAsView, dstAsView;

	if (command.typeAS == AccelerationStructure::Type::Blas)
	{
		srcAsView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.srcAS)));
		copyInfo.src = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
		dstAsView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.dstAS)));
		copyInfo.dst = (VkAccelerationStructureKHR)ResourceExt::getInstance(**dstAsView);
	}
	else
	{
		srcAsView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.srcAS)));
		copyInfo.src = (VkAccelerationStructureKHR)ResourceExt::getInstance(**srcAsView);
		dstAsView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.dstAS)));
		copyInfo.dst = (VkAccelerationStructureKHR)ResourceExt::getInstance(**dstAsView);
	}

	Buffer::BarrierState srcNewState, dstNewState;
	srcNewState.stage = dstNewState.stage = (uint32)vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR;
	srcNewState.access = (uint32)vk::AccessFlagBits::eAccelerationStructureReadKHR;
	dstNewState.access = (uint32)vk::AccessFlagBits::eAccelerationStructureWriteKHR;
	auto& srcOldState = AccelerationStructureExt::getBarrierState(**srcAsView);
	auto& dstOldState = AccelerationStructureExt::getBarrierState(**dstAsView);
	addMemoryBarrier(vulkanAPI, srcOldState, dstOldState, srcNewState, dstNewState);
	processPipelineBarriers(vulkanAPI);

	instance.copyAccelerationStructureKHR(copyInfo);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const TraceRaysCommand& command)
{
	SET_CPU_ZONE_SCOPED("TraceRays Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto commandBufferData = (const uint8*)&command;
	auto commandOffset = (int64)(commandBufferData - data) - (int64)command.lastSize;

	Buffer::BarrierState newSrcBufferState;
	newSrcBufferState.access = (uint32)vk::AccessFlagBits::eShaderRead;
	newSrcBufferState.stage = (uint32)vk::PipelineStageFlagBits::eRayTracingShaderKHR;
	addBufferBarrier(vulkanAPI, newSrcBufferState, command.sbtBuffer);

	while (commandOffset >= 0)
	{
		auto subCommand = (const Command*)(data + commandOffset);
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);
		if (subCommand->lastSize == 0)
			break;

		auto commandType = subCommand->type;
		if (commandType == Command::Type::TraceRays | commandType == Command::Type::Dispatch |
			commandType == Command::Type::EndRenderPass)
		{
			break;
		}

		commandOffset -= subCommand->lastSize;
		if (commandType != Command::Type::BindDescriptorSets)
			continue;

		const auto& bindDescriptorSetsCommand = *(const BindDescriptorSetsCommand*)subCommand;
		auto descriptorSetRange = (const DescriptorSet::Range*)(
			(const uint8*)subCommand + sizeof(BindDescriptorSetsCommandBase));
		addDescriptorSetBarriers(vulkanAPI, descriptorSetRange, bindDescriptorSetsCommand.rangeCount);
	}

	processPipelineBarriers(vulkanAPI);

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

#if GARDEN_DEBUG
//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BeginLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginLabel Command Process");

	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	array<float, 4> values;
	*(float4*)values.data() = (float4)command.color;
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
	array<float, 4> values;
	*(float4*)values.data() = (float4)command.color;
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	instance.beginDebugUtilsLabelEXT(debugLabel);
}
#endif