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
	case CommandBufferType::ComputeOnly:
		instance = createVkCommandBuffer(vulkanAPI->device, vulkanAPI->computeCommandPool); break;
	case CommandBufferType::Frame: break;
	default: abort();
	}

	#if GARDEN_DEBUG
	if (vulkanAPI->hasDebugUtils)
	{
		const char* name = nullptr;
		switch (type)
		{
		case CommandBufferType::Graphics: name = "commandBuffer.graphics"; break;
		case CommandBufferType::TransferOnly: name = "commandBuffer.transferOnly"; break;
		case CommandBufferType::ComputeOnly: name = "commandBuffer.computeOnly"; break;
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
constexpr uint32 writeAccessMask =
	VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
	VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
	VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT;

constexpr bool isDifferentState(const Image::BarrierState& oldState, const Image::BarrierState& newState) noexcept
{
	return oldState.layout != newState.layout || (oldState.access & writeAccessMask);
}
constexpr bool isDifferentState(const Buffer::BarrierState& oldState) noexcept
{
	return oldState.access & writeAccessMask;
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
static void addImageBarrier(VulkanAPI* vulkanAPI, 
	const Image::BarrierState& newImageState, ID<ImageView> imageView, uint32& oldPipelineStage, 
	vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor)
{
	auto view = vulkanAPI->imageViewPool.get(imageView);
	auto image = vulkanAPI->imagePool.get(view->getImage());
	auto& oldImageState = vulkanAPI->getImageState(
		view->getImage(), view->getBaseMip(), view->getBaseLayer());

	if (isDifferentState(oldImageState, newImageState))
	{
		addImageBarrier(vulkanAPI, oldImageState, newImageState, (VkImage)ResourceExt::getInstance(
			**image), view->getBaseMip(), 1, view->getBaseLayer(), 1, aspectFlags);
	}
	oldPipelineStage |= oldImageState.stage;
	oldImageState = newImageState;

	ImageExt::isFullBarrier(**image) = image->getMipCount() == 1 && image->getLayerCount() == 1;
}

static void addImageBarriers(VulkanAPI* vulkanAPI, const Image::BarrierState& newImageState, ID<Image> image, 
	uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount, uint32& oldPipelineStage)
{
	auto imageView = vulkanAPI->imagePool.get(image);
	auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
	auto aspectFlags = toVkImageAspectFlags(imageView->getFormat());

	auto newFullBarrier = baseMip == 0 && mipCount == imageView->getMipCount() &&
		baseLayer == 0 && layerCount == imageView->getLayerCount();
	if (ImageExt::isFullBarrier(**imageView) && newFullBarrier)
	{
		auto& oldImageState = vulkanAPI->getImageState(image, 0, 0);
		if (isDifferentState(oldImageState, newImageState))
		{
			addImageBarrier(vulkanAPI, oldImageState, newImageState, 
				vkImage, 0, mipCount, 0, layerCount, aspectFlags);
		}
		oldPipelineStage |= oldImageState.stage;

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
				if (isDifferentState(oldImageState, newImageState))
				{
					addImageBarrier(vulkanAPI, oldImageState, newImageState, 
						vkImage, mip, 1, layer, 1, aspectFlags);
				}
				oldPipelineStage |= oldImageState.stage;
				oldImageState = newImageState;
			}
		}
	}
	ImageExt::isFullBarrier(**imageView) = newFullBarrier;
}

static void addBufferBarrier(VulkanAPI* vulkanAPI, const Buffer::BarrierState& newBufferState, 
	ID<Buffer> buffer, uint32& oldPipelineStage, uint64 size = VK_WHOLE_SIZE, uint64 offset = 0)
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
	}

	oldPipelineStage |= oldBufferState.stage;
	oldBufferState = newBufferState;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addDescriptorSetBarriers(const DescriptorSet::Range* descriptorSetRange,
	uint32 rangeCount, uint32& oldStage, uint32& newStage)
{
	SET_CPU_ZONE_SCOPED("Descriptor Set Barriers Add");

	// also add to the shaders noncoherent tag to skip sync if different buffer or image parts.
	auto vulkanAPI = VulkanAPI::get();
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		auto descriptorSet = vulkanAPI->descriptorSetPool.get(descriptor.set);
		const auto& descriptorSetUniforms = descriptorSet->getUniforms();
		auto pipelineView = vulkanAPI->getPipelineView(
			descriptorSet->getPipelineType(), descriptorSet->getPipeline());
		const auto& pipelineUniforms = pipelineView->getUniforms();

		for (const auto& pipelineUniform : pipelineUniforms)
		{
			auto uniform = pipelineUniform.second;
			if (uniform.descriptorSetIndex != i)
				continue;

			auto setCount = descriptor.offset + descriptor.count;
			const auto& descriptorSetUniform = descriptorSetUniforms.at(pipelineUniform.first);

			if (isSamplerType(uniform.type) || isImageType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Image/Sampler Barriers Process");

				Image::BarrierState newImageState;
				newImageState.stage = (uint32)toVkPipelineStages(uniform.shaderStages);
				newPipelineStage |= newImageState.stage;

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
						const auto& resourceArray = descriptorSetUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							if (!resource)
								continue;

							auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), 
								imageView->getBaseMip(), imageView->getMipCount(), 
								imageView->getBaseLayer(), imageView->getLayerCount(), oldPipelineStage);
						}
					}
				}
				else
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = descriptorSetUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resource));
							addImageBarriers(vulkanAPI, newImageState, imageView->getImage(), 
								imageView->getBaseMip(), imageView->getMipCount(), 
								imageView->getBaseLayer(), imageView->getLayerCount(), oldPipelineStage);
						}
					}
				}
			}
			else if (isBufferType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Buffer Barriers Process");

				Buffer::BarrierState newBufferState;
				newBufferState.stage = (uint32)toVkPipelineStages(uniform.shaderStages);
				newPipelineStage |= newBufferState.stage;

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
						const auto& resourceArray = descriptorSetUniform.resourceSets[j];
						for (auto resource : resourceArray)
						{
							if (!resource)
								continue;
							addBufferBarrier(vulkanAPI, newBufferState, ID<Buffer>(resource), oldPipelineStage);
						}
					}
				}
				else
				{
					for (uint32 j = descriptor.offset; j < setCount; j++)
					{
						const auto& resourceArray = descriptorSetUniform.resourceSets[j];
						for (auto resource : resourceArray)
							addBufferBarrier(vulkanAPI, newBufferState, ID<Buffer>(resource), oldPipelineStage);
					}
				}
			}
			else if (uniform.type == GslUniformType::AccelerationStructure)
			{
				// Note: assuming that acceleration structure will be built on a separate compute queue.
			}
			else abort();
		}
	}

	oldStage |= oldPipelineStage;
	newStage |= newPipelineStage;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processPipelineBarriers(uint32 oldStage, uint32 newStage)
{
	SET_CPU_ZONE_SCOPED("Pipeline Barriers Process");

	auto vulkanAPI = VulkanAPI::get();
	if (vulkanAPI->imageMemoryBarriers.empty() && vulkanAPI->bufferMemoryBarriers.empty())
		return;

	auto oldPipelineStage = (vk::PipelineStageFlagBits)oldStage;
	auto newPipelineStage = (vk::PipelineStageFlagBits)newStage;

	if (oldPipelineStage == vk::PipelineStageFlagBits::eNone)
		oldPipelineStage = vk::PipelineStageFlagBits::eTopOfPipe;

	// TODO: combine overlapping barriers.
	// TODO: use dependency flags

	instance.pipelineBarrier(oldPipelineStage, newPipelineStage, {}, 0, nullptr,
		(uint32)vulkanAPI->bufferMemoryBarriers.size(), vulkanAPI->bufferMemoryBarriers.data(),
		(uint32)vulkanAPI->imageMemoryBarriers.size(), vulkanAPI->imageMemoryBarriers.data());

	vulkanAPI->imageMemoryBarriers.clear();
	vulkanAPI->bufferMemoryBarriers.clear();
}

//**********************************************************************************************************************
void VulkanCommandBuffer::submit()
{
	SET_CPU_ZONE_SCOPED("Command Buffer Submit");

	auto vulkanAPI = VulkanAPI::get();
	auto swapchain = vulkanAPI->vulkanSwapchain;
	auto swapchainBuffer = swapchain->getCurrentVkBuffer();

	vk::Queue queue;
	if (type == CommandBufferType::Frame)
	{
		instance = swapchainBuffer->primaryCommandBuffer;
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
		else if (type == CommandBufferType::ComputeOnly)
			queue = vulkanAPI->computeQueue;
		else
			queue = vulkanAPI->graphicsQueue;
	}

	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	instance.begin(beginInfo);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (type == CommandBufferType::Frame && vulkanAPI->recordGpuTime)
	{
		instance.resetQueryPool(swapchainBuffer->queryPool, 0, 2);
		instance.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, swapchainBuffer->queryPool, 0);
		swapchainBuffer->isPoolClean = true;
		// TODO: record transfer and compute command buffer perf.
	}
	#endif

	processCommands();

	if (type == CommandBufferType::Frame)
	{
		auto imageView = vulkanAPI->imagePool.get(swapchainBuffer->colorImage);
		auto vkImage = (VkImage)ResourceExt::getInstance(**imageView);
		auto& oldImageState = vulkanAPI->getImageState(swapchainBuffer->colorImage, 0, 0);

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
			instance.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, swapchainBuffer->queryPool, 1);
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
	
	auto& imagePool = vulkanAPI->imagePool;
	for (auto& image : imagePool)
	{
		auto& states = ImageExt::getBarrierStates(image);
		for (auto& state : states)
			state.access = state.stage = 0;
	}
	{
		auto swapchainView = imagePool.get(swapchainBuffer->colorImage);
		auto& states = ImageExt::getBarrierStates(**swapchainView);
		for (auto& state : states)
			state.stage = (uint32)vk::PipelineStageFlagBits::eBottomOfPipe;
	}

	auto& bufferPool = vulkanAPI->bufferPool;
	for (auto& buffer : bufferPool)
		BufferExt::getBarrierState(buffer) = {};

	std::swap(lockingResources, lockedResources);
	size = lastSize = 0;
	hasAnyCommand = false;
	isRunning = true;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addRenderPassBarriers(psize commandOffset, uint32& oldPipelineStage, uint32& newPipelineStage)
{
	SET_CPU_ZONE_SCOPED("Render Pass Barriers Add");

	auto vulkanAPI = VulkanAPI::get();
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
			addDescriptorSetBarriers(descriptorSetRange,
				bindDescriptorSetsCommand.rangeCount, oldPipelineStage, newPipelineStage);
		}
		else if (commandType == Command::Type::Draw)
		{
			const auto& drawCommand = *(const DrawCommand*)subCommand;
			if (!drawCommand.vertexBuffer)
				continue;

			Buffer::BarrierState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;
			addBufferBarrier(vulkanAPI, newBufferState, drawCommand.vertexBuffer, oldPipelineStage);
		}
		else if (commandType == Command::Type::DrawIndexed)
		{
			const auto& drawIndexedCommand = *(const DrawIndexedCommand*)subCommand;
			
			Buffer::BarrierState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;
			addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand.vertexBuffer, oldPipelineStage);

			newBufferState.access |= (uint32)vk::AccessFlagBits::eIndexRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;
			addBufferBarrier(vulkanAPI, newBufferState, drawIndexedCommand.indexBuffer, oldPipelineStage);
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
void VulkanCommandBuffer::processCommand(const BeginRenderPassCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginRenderPass Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto framebuffer = vulkanAPI->framebufferPool.get(command.framebuffer);
	const auto& colorAttachments = framebuffer->getColorAttachments().data();
	auto colorAttachmentCount = (uint32)framebuffer->getColorAttachments().size();
	auto commandBufferData = (const uint8*)&command;
	auto clearColors = (const float4*)(commandBufferData + sizeof(BeginRenderPassCommandBase));
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

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
		newPipelineStage |= newImageState.stage;
		addImageBarrier(vulkanAPI, newImageState, colorAttachment.imageView, oldPipelineStage);
		
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
	
	vk::RenderingAttachmentInfoKHR depthAttachmentInfo;
	vk::RenderingAttachmentInfoKHR stencilAttachmentInfo;
	vk::RenderingAttachmentInfoKHR* depthAttachmentInfoPtr = nullptr;
	vk::RenderingAttachmentInfoKHR* stencilAttachmentInfoPtr = nullptr;

	if (framebuffer->getDepthStencilAttachment().imageView)
	{
		auto depthStencilAttachment = framebuffer->getDepthStencilAttachment();
		
		Image::BarrierState newImageState;
		newImageState.layout = (uint32)vk::ImageLayout::eDepthStencilAttachmentOptimal;
		if (depthStencilAttachment.flags.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentRead;
		if (depthStencilAttachment.flags.clear || depthStencilAttachment.flags.load)
			newImageState.stage |= (uint32)vk::PipelineStageFlagBits::eEarlyFragmentTests;
		if (depthStencilAttachment.flags.store)
		{
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			newImageState.stage |= (uint32)vk::PipelineStageFlagBits::eLateFragmentTests;
		}
		newPipelineStage |= newImageState.stage;

		auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
		auto imageFormat = imageView->getFormat();
		vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

		if (isFormatDepthOnly(imageFormat))
		{
			newImageState.layout = (uint32)vk::ImageLayout::eDepthAttachmentOptimal;
			aspectFlags = vk::ImageAspectFlagBits::eDepth;

			if (noSubpass)
			{
				if (depthStencilAttachment.flags.clear) // TODO: support separated depth/stencil clear?
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
					depthAttachmentInfo.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(
						command.clearDepth, command.clearStencil));
				}
				else if (depthStencilAttachment.flags.load)
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
				}
				else
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eDontCare;
				}

				depthAttachmentInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
				depthAttachmentInfo.storeOp = depthStencilAttachment.flags.store ? 
					vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eNone;
				depthAttachmentInfoPtr = &depthAttachmentInfo;
			}
			else
			{
				vulkanAPI->clearValues.emplace_back(vk::ClearDepthStencilValue(
					command.clearDepth, command.clearStencil));
			}
		}
		else if (isFormatStencilOnly(imageFormat))
		{
			newImageState.layout = (uint32)vk::ImageLayout::eStencilAttachmentOptimal;
			newImageState.stage = (uint32)vk::PipelineStageFlagBits::eLateFragmentTests; // Do not remove!
			aspectFlags = vk::ImageAspectFlagBits::eStencil;

			if (noSubpass)
			{
				if (depthStencilAttachment.flags.clear)
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
					stencilAttachmentInfo.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(
						command.clearDepth, command.clearStencil));
				}
				else if (depthStencilAttachment.flags.load)
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
				}
				else
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eDontCare;
				}

				stencilAttachmentInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				stencilAttachmentInfo.imageLayout = vk::ImageLayout::eStencilAttachmentOptimal;
				stencilAttachmentInfo.storeOp = depthStencilAttachment.flags.store ? 
					vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eNone;
				stencilAttachmentInfoPtr = &stencilAttachmentInfo;
			}
			else
			{
				vulkanAPI->clearValues.emplace_back(vk::ClearDepthStencilValue(
					command.clearDepth, command.clearStencil));
			}
		}

		addImageBarrier(vulkanAPI, newImageState, 
			depthStencilAttachment.imageView, oldPipelineStage, aspectFlags);
	}

	auto commandOffset = (commandBufferData - data) + command.thisSize;
	addRenderPassBarriers(commandOffset, oldPipelineStage, newPipelineStage);
	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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
			GARDEN_ASSERT(framebufferView->getDepthStencilAttachment().imageView);
			attachmentView = framebufferView->getDepthStencilAttachment().imageView;
		}
		else abort();

		auto imageView = vulkanAPI->imageViewPool.get(attachmentView);
		auto format = imageView->getFormat();

		vk::ClearValue clearValue;
		if (isFormatFloat(format))
		{
			memcpy(clearValue.color.float32.data(), &attachment.clearColor.floatValue, sizeof(float) * 4);
		}
		else if (isFormatInt(format))
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

	auto commandBufferData = (const uint8*)&command;
	auto commandOffset = (int64)(commandBufferData - data) - (int64)command.lastSize;
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

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
		addDescriptorSetBarriers(descriptorSetRange,
			bindDescriptorSetsCommand.rangeCount, oldPipelineStage, newPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);
	instance.dispatch(command.groupCount.x, command.groupCount.y, command.groupCount.z);
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const FillBufferCommand& command)
{
	SET_CPU_ZONE_SCOPED("FillBuffer Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto bufferView = vulkanAPI->bufferPool.get(command.buffer);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**bufferView);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	Buffer::BarrierState newBufferState;
	newBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newBufferState.stage = newPipelineStage;

	addBufferBarrier(vulkanAPI, newBufferState, command.buffer, oldPipelineStage,
		command.size == 0 ? VK_WHOLE_SIZE : command.size, command.offset);
	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (vulkanAPI->bufferCopies.size() < command.regionCount)
		vulkanAPI->bufferCopies.resize(command.regionCount);

	Buffer::BarrierState newSrcBufferState;
	newSrcBufferState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcBufferState.stage = newPipelineStage;

	Buffer::BarrierState newDstBufferState;
	newDstBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstBufferState.stage = newPipelineStage;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		vulkanAPI->bufferCopies[i] = vk::BufferCopy(region.srcOffset, region.dstOffset,
			region.size == 0 ? srcBuffer->getBinarySize() : region.size);
		addBufferBarrier(vulkanAPI, newSrcBufferState, command.source, oldPipelineStage);
		addBufferBarrier(vulkanAPI, newDstBufferState, command.destination, oldPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);
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
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (vulkanAPI->imageClears.size() < command.regionCount)
		vulkanAPI->imageClears.resize(command.regionCount);

	Image::BarrierState newImageState;
	newImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = newPipelineStage;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		auto mipCount = region.mipCount == 0 ? image->getMipCount() : region.mipCount;
		auto layerCount = region.layerCount == 0 ? image->getLayerCount() : region.layerCount;

		vulkanAPI->imageClears[i] = vk::ImageSubresourceRange(aspectFlags,
			region.baseMip, mipCount, region.baseLayer, layerCount);
		addImageBarriers(vulkanAPI, newImageState, command.image, 
			region.baseMip, mipCount, region.baseLayer, layerCount, oldPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (vulkanAPI->imageCopies.size() < command.regionCount)
		vulkanAPI->imageCopies.resize(command.regionCount);

	Image::BarrierState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = newPipelineStage;

	Image::BarrierState newDstImageState;
	newDstImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newDstImageState.stage = newPipelineStage;

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
				srcImage->getType() == Image::Type::Texture3D ? srcImage->getSize().getZ() : 1);
		}
		else
		{
			extent = vk::Extent3D(region.extent.x, region.extent.y, region.extent.z);
		}

		vulkanAPI->imageCopies[i] = vk::ImageCopy(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);

		addImageBarriers(vulkanAPI, newSrcImageState, command.source, 
			region.srcMipLevel, 1, region.srcBaseLayer, srcSubresource.layerCount, oldPipelineStage);
		addImageBarriers(vulkanAPI, newDstImageState, command.destination, 
			region.dstMipLevel, 1, region.dstBaseLayer, dstSubresource.layerCount, oldPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (vulkanAPI->bufferImageCopies.size() < command.regionCount)
		vulkanAPI->bufferImageCopies.resize(command.regionCount);

	Buffer::BarrierState newBufferState;
	newBufferState.access = command.toBuffer ?
		(uint32)vk::AccessFlagBits::eTransferWrite : (uint32)vk::AccessFlagBits::eTransferRead;
	newBufferState.stage = newPipelineStage;

	Image::BarrierState newImageState;
	newImageState.access = command.toBuffer ?
		(uint32)vk::AccessFlagBits::eTransferRead : (uint32)vk::AccessFlagBits::eTransferWrite;
	newImageState.layout = command.toBuffer ?
		(uint32)vk::ImageLayout::eTransferSrcOptimal : (uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = newPipelineStage;

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
				image->getType() == Image::Type::Texture3D ? image->getSize().getZ() : 1);
		}
		else
		{
			dstExtent = vk::Extent3D(region.imageExtent.x, region.imageExtent.y, region.imageExtent.z);
		}

		vulkanAPI->bufferImageCopies[i] = vk::BufferImageCopy((vk::DeviceSize)region.bufferOffset,
			region.bufferRowLength, region.bufferImageHeight, imageSubresource, dstOffset, dstExtent);
		addBufferBarrier(vulkanAPI, newBufferState, command.buffer, oldPipelineStage);

		addImageBarriers(vulkanAPI, newImageState, command.image, region.imageMipLevel, 1, 
			region.imageBaseLayer, imageSubresource.layerCount, oldPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (vulkanAPI->imageBlits.size() < command.regionCount)
		vulkanAPI->imageBlits.resize(command.regionCount);

	Image::BarrierState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = newPipelineStage;

	Image::BarrierState newDstImageState;
	newDstImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newDstImageState.stage = newPipelineStage;

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
				srcImage->getType() == Image::Type::Texture3D ? srcImage->getSize().getZ() : 1);
		}
		else
		{
			srcBounds[1] = vk::Offset3D(region.srcExtent.x, region.srcExtent.y, region.srcExtent.z);
		}
		if (region.dstExtent == uint3::zero)
		{
			auto mipImageSize = calcSizeAtMip3(dstImage->getSize(), region.dstMipLevel);
			dstBounds[1] = vk::Offset3D(mipImageSize.getX(), mipImageSize.getY(), 
				dstImage->getType() == Image::Type::Texture3D ? dstImage->getSize().getZ() : 1);
		}
		else
		{
			dstBounds[1] = vk::Offset3D(region.dstExtent.x, region.dstExtent.y, region.dstExtent.z);
		}

		vulkanAPI->imageBlits[i] = vk::ImageBlit(srcSubresource, srcBounds, dstSubresource, dstBounds);

		addImageBarriers(vulkanAPI, newSrcImageState, command.source, 
			region.srcMipLevel, 1, region.srcBaseLayer, srcSubresource.layerCount, oldPipelineStage);
		addImageBarriers(vulkanAPI, newDstImageState, command.destination, 
			region.dstMipLevel, 1, region.dstBaseLayer, dstSubresource.layerCount, oldPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

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

void VulkanCommandBuffer::processCommand(const BuildAccelerationStructureCommand& command)
{
	SET_CPU_ZONE_SCOPED("BuildAccelerationStructure Command Process");

	auto vulkanAPI = VulkanAPI::get();
	auto scratchBufferView = vulkanAPI->bufferPool.get(command.scratchBuffer);

	uint32 newPipelineStage = (uint32)vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR;
	Buffer::BarrierState newBufferState;
	newBufferState.access = (uint32)(vk::AccessFlagBits::eAccelerationStructureReadKHR |
		vk::AccessFlagBits::eAccelerationStructureWriteKHR);
	newBufferState.stage = newPipelineStage;
	addBufferBarrier(vulkanAPI, newBufferState, command.scratchBuffer, vulkanAPI->asOldPipelineStage); // TODO: synchronize only used part of the scratch buffer.

	vk::AccelerationStructureBuildGeometryInfoKHR info;
	View<AccelerationStructure> asView;
	if (command.srcAS)
	{
		if (command.typeAS == AccelerationStructure::Type::Blas)
			asView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.srcAS)));
		else
			asView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.srcAS)));

		addBufferBarrier(vulkanAPI, newBufferState, AccelerationStructureExt::
			getStorage(**asView), vulkanAPI->asOldPipelineStage);
		info.srcAccelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**asView);
	}
	if (command.typeAS == AccelerationStructure::Type::Blas)
	{
		info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		asView = View<AccelerationStructure>(vulkanAPI->blasPool.get(ID<Blas>(command.dstAS)));
	}
	else
	{
		info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
		asView = View<AccelerationStructure>(vulkanAPI->tlasPool.get(ID<Tlas>(command.dstAS)));
	}

	addBufferBarrier(vulkanAPI, newBufferState, AccelerationStructureExt::
		getStorage(**asView), vulkanAPI->asOldPipelineStage);
	
	auto buildData = (const uint8*)AccelerationStructureExt::getBuildData(**asView);
	auto buildDataHeader = *((const AccelerationStructure::BuildDataHeader*)buildData);
	auto builDataOffset = sizeof(AccelerationStructure::BuildDataHeader);
	auto syncBuffers = (ID<Buffer>*)(buildData + builDataOffset);
	builDataOffset += buildDataHeader.bufferCount * sizeof(ID<Buffer>);
	auto asArray = (const vk::AccelerationStructureGeometryKHR*)(buildData + builDataOffset);
	builDataOffset += buildDataHeader.geometryCount * sizeof(vk::AccelerationStructureGeometryKHR);
	auto rangeInfos = (const vk::AccelerationStructureBuildRangeInfoKHR*)(buildData + builDataOffset);

	info.flags = toVkBuildFlagsAS(asView->getFlags());
	info.mode = command.isUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate :
		vk::BuildAccelerationStructureModeKHR::eBuild;
	info.dstAccelerationStructure = (VkAccelerationStructureKHR)ResourceExt::getInstance(**asView);
	info.scratchData = alignSize(scratchBufferView->getDeviceAddress(), 
		(uint64)vulkanAPI->asProperties.minAccelerationStructureScratchOffsetAlignment);
	info.geometryCount = buildDataHeader.geometryCount;
	info.pGeometries = asArray;

	newBufferState.access = (uint32)vk::AccessFlagBits::eShaderRead;
	for (uint32 i = 0; i < buildDataHeader.bufferCount; i++)
		addBufferBarrier(vulkanAPI, newBufferState, syncBuffers[i], vulkanAPI->asOldPipelineStage);

	vulkanAPI->asBuildData.push_back(&AccelerationStructureExt::getBuildData(**asView));
	vulkanAPI->asGeometryInfos.push_back(info);
	vulkanAPI->asRangeInfos.push_back(rangeInfos);
	
	auto commandOffset = (((const uint8*)&command) - data) + command.thisSize;
	auto shouldBuild = false;
	if (commandOffset < size)
	{
		auto subCommand = (const Command*)(data + commandOffset);
		GARDEN_ASSERT(subCommand->type < Command::Type::Count);

		if (subCommand->type != Command::Type::BuildAccelerationStructure)
			shouldBuild = true;

		auto subCommandAS = (const BuildAccelerationStructureCommand*)subCommand;
		if (command.typeAS != subCommandAS->typeAS) // We can't mix BLAS and TLAS in one command.
			shouldBuild = true;
	}
	else
	{
		shouldBuild = true;
	}

	if (shouldBuild)
	{
		processPipelineBarriers(vulkanAPI->asOldPipelineStage, newPipelineStage);

		instance.buildAccelerationStructuresKHR(vulkanAPI->asGeometryInfos.size(), 
			vulkanAPI->asGeometryInfos.data(), vulkanAPI->asRangeInfos.data());

		for (auto buildData : vulkanAPI->asBuildData)
		{
			free(*buildData);
			*buildData = nullptr;
		}
		vulkanAPI->asBuildData.clear();

		vulkanAPI->asGeometryInfos.clear();
		vulkanAPI->asRangeInfos.clear();
		vulkanAPI->asOldPipelineStage = 0;
	}
}

//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const TraceRaysCommand& command)
{
	SET_CPU_ZONE_SCOPED("TraceRays Command Process");

	auto commandBufferData = (const uint8*)&command;
	auto commandOffset = (int64)(commandBufferData - data) - (int64)command.lastSize;
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

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
		addDescriptorSetBarriers(descriptorSetRange,
			bindDescriptorSetsCommand.rangeCount, oldPipelineStage, newPipelineStage);
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	vk::StridedDeviceAddressRegionKHR rayGenRegion(command.sbt.rayGenRegion.deviceAddress,
		command.sbt.rayGenRegion.stride, command.sbt.rayGenRegion.size);
	vk::StridedDeviceAddressRegionKHR missRegion(command.sbt.missRegion.deviceAddress,
		command.sbt.missRegion.stride, command.sbt.missRegion.size);
	vk::StridedDeviceAddressRegionKHR hitRegion(command.sbt.hitRegion.deviceAddress,
		command.sbt.hitRegion.stride, command.sbt.hitRegion.size);
	vk::StridedDeviceAddressRegionKHR callRegion(command.sbt.callRegion.deviceAddress,
		command.sbt.callRegion.stride, command.sbt.callRegion.size);

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