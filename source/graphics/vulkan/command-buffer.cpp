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
			vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
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
	VK_ACCESS_SHADER_WRITE_BIT |
	VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT |
	VK_ACCESS_MEMORY_WRITE_BIT |
	VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
	VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
	VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;

constexpr bool isSameState(const CommandBuffer::ImageState& oldState, 
	const CommandBuffer::ImageState& newState) noexcept
{
	return !(oldState.layout != newState.layout || (oldState.access & writeAccessMask));
}
constexpr bool isSameState(const CommandBuffer::BufferState& oldState, 
	const CommandBuffer::BufferState& newState) noexcept
{
	return !(oldState.access & writeAccessMask);
}

//**********************************************************************************************************************
CommandBuffer::ImageState& VulkanCommandBuffer::getImageState(ID<Image> image, uint32 mip, uint32 layer)
{
	ImageSubresource imageSubresource;
	imageSubresource.image = image;
	imageSubresource.mip = mip;
	imageSubresource.layer = layer;

	auto searchResult = imageStates.find(imageSubresource); 
	if (searchResult != imageStates.end())
	{
		return searchResult->second;
	}
	else
	{
		auto imageView = VulkanAPI::get()->imagePool.get(image);
		const auto& layouts = ImageExt::getLayouts(**imageView);
		ImageState newImageState;
		newImageState.access = (uint32)VK_ACCESS_NONE;
		newImageState.layout = layouts[mip * imageView->getLayerCount() + layer];
		newImageState.stage = imageView->isSwapchain() ?
			(uint32)vk::PipelineStageFlagBits::eBottomOfPipe : (uint32)vk::PipelineStageFlagBits::eNone;
		auto addResult = imageStates.emplace(imageSubresource, newImageState);
		return addResult.first->second;
	}
}

CommandBuffer::BufferState& VulkanCommandBuffer::getBufferState(ID<Buffer> buffer)
{
	auto searchResult = bufferStates.find(buffer); 
	if (searchResult != bufferStates.end())
	{
		return searchResult->second;
	}
	else
	{
		BufferState newBufferState;
		newBufferState.access = (uint32)VK_ACCESS_NONE;
		newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eNone;
		auto addResult = bufferStates.emplace(buffer, newBufferState);
		return addResult.first->second;
	}
}

//**********************************************************************************************************************
static void addImageBarrier(const CommandBuffer::ImageState& oldImageState, 
	const CommandBuffer::ImageState& newImageState, vk::Image image, 
	uint32 baseMip, uint32 mipCount, uint32 baseLayer, uint32 layerCount,
	vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor)
{
	vk::ImageMemoryBarrier imageMemoryBarrier(
		vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
		vk::ImageLayout(oldImageState.layout), vk::ImageLayout(newImageState.layout),
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, vk::ImageSubresourceRange(
			aspectFlags, baseMip, mipCount, baseLayer, layerCount));
	VulkanAPI::get()->imageMemoryBarriers.push_back(imageMemoryBarrier);
}
static void addImageBarrier(const CommandBuffer::ImageState& oldImageState,
	const CommandBuffer::ImageState& newImageState, View<ImageView> imageView,
	vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor)
{
	// TODO: we can transfer only required for rendering mips and layers,
	//		 instead of all image data like this.
	auto view = VulkanAPI::get()->imagePool.get(imageView->getImage());
	addImageBarrier(oldImageState, newImageState, (VkImage)
		ResourceExt::getInstance(**view), imageView->getBaseMip(), imageView->getMipCount(), 
		imageView->getBaseLayer(), imageView->getLayerCount(), aspectFlags);
}

static void addBufferBarrier(const CommandBuffer::BufferState& oldBufferState,
	const CommandBuffer::BufferState& newBufferState, vk::Buffer buffer, uint64 size, uint64 offset = 0)
{
	vk::BufferMemoryBarrier bufferMemoryBarrier(
		vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, buffer, offset, size);
	VulkanAPI::get()->bufferMemoryBarriers.push_back(bufferMemoryBarrier);
}
static void addBufferBarrier(const CommandBuffer::BufferState& oldBufferState, 
	const CommandBuffer::BufferState& newBufferState, ID<Buffer> buffer)
{
	// TODO: we can specify only required buffer range, not full range.
	auto vulkanAPI = VulkanAPI::get();
	auto bufferView = vulkanAPI->bufferPool.get(buffer);
	addBufferBarrier(oldBufferState, newBufferState, (VkBuffer)
		ResourceExt::getInstance(**bufferView), VK_WHOLE_SIZE);
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
				SET_CPU_ZONE_SCOPED("Sampler/Image Barriers Process");

				ImageState newImageState;
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

				for (uint32 j = descriptor.offset; j < setCount; j++)
				{
					const auto& resourceArray = descriptorSetUniform.resourceSets[j];
					for (uint32 k = 0; k < (uint32)resourceArray.size(); k++)
					{
						if (!resourceArray[k])
							continue; // TODO: maybe separate into 2 paths: bindless/nonbindless?

						auto imageView = vulkanAPI->imageViewPool.get(ID<ImageView>(resourceArray[k]));
						auto instance = imageView->getImage();
						auto image = vulkanAPI->imagePool.get(instance);
						auto vkImage = (VkImage)ResourceExt::getInstance(**image);
						auto imageAspectFlags = toVkImageAspectFlags(imageView->getFormat());
						auto baseMip = imageView->getBaseMip();
						auto mipCount = baseMip + imageView->getMipCount();
						auto baseLayer = imageView->getBaseLayer();
						auto layerCount = baseLayer + imageView->getLayerCount();
						newPipelineStage |= newImageState.stage;

						for (uint8 mip = baseMip; mip < mipCount; mip++)
						{
							for (uint32 layer = baseLayer; layer < layerCount; layer++)
							{
								auto& oldImageState = getImageState(instance, mip, layer);
								oldPipelineStage |= oldImageState.stage;

								if (!isSameState(oldImageState, newImageState))
								{
									addImageBarrier(oldImageState, newImageState, 
										vkImage, mip, 1, layer, 1, imageAspectFlags);
								}
								oldImageState = newImageState;
							}
						}
					}
				}
			}
			else if (isBufferType(uniform.type))
			{
				SET_CPU_ZONE_SCOPED("Buffer Barriers Process");

				BufferState newBufferState;
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

				for (uint32 j = descriptor.offset; j < setCount; j++)
				{
					const auto& resourceArray = descriptorSetUniform.resourceSets[j];
					for (uint32 k = 0; k < (uint32)resourceArray.size(); k++)
					{
						auto buffer = ID<Buffer>(resourceArray[k]);
						auto& oldBufferState = getBufferState(buffer);
						oldPipelineStage |= oldBufferState.stage;
						newPipelineStage |= newBufferState.stage;

						if (!isSameState(oldBufferState, newBufferState))
							addBufferBarrier(oldBufferState, newBufferState, buffer);
						oldBufferState = newBufferState;
					}
				}
			}
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
		auto& oldImageState = getImageState(swapchainBuffer->colorImage, 0, 0);

		if (!hasAnyCommand)
		{
			ImageState newImageState;
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

		ImageState newImageState;
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
	
	updateImageStates();
	std::swap(lockingResources, lockedResources);
	size = lastSize = 0;
	hasAnyCommand = false;
	isRunning = true;
}

//**********************************************************************************************************************
void VulkanCommandBuffer::addRenderPassBarriers(psize offset, uint32& oldPipelineStage, uint32& newPipelineStage)
{
	SET_CPU_ZONE_SCOPED("Render Pass Barriers Add");

	while (offset < size)
	{
		auto subCommand = (const Command*)(data + offset);
		GARDEN_ASSERT((uint8)subCommand->type < (uint8)Command::Type::Count);

		auto commandType = subCommand->type;
		if (commandType == Command::Type::EndRenderPass)
			break;

		offset += subCommand->thisSize;

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
			auto& oldBufferState = getBufferState(drawCommand.vertexBuffer);
			oldPipelineStage |= oldBufferState.stage;

			BufferState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;

			if (!isSameState(oldBufferState, newBufferState))
				addBufferBarrier(oldBufferState, newBufferState, drawCommand.vertexBuffer);
			oldBufferState = newBufferState;
		}
		else if (commandType == Command::Type::DrawIndexed)
		{
			const auto& drawIndexedCommand = *(const DrawIndexedCommand*)subCommand;
			auto& oldVertexBufferState = getBufferState(drawIndexedCommand.vertexBuffer);
			oldPipelineStage |= oldVertexBufferState.stage;

			BufferState newBufferState;
			newBufferState.access |= (uint32)vk::AccessFlagBits::eVertexAttributeRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;

			if (!isSameState(oldVertexBufferState, newBufferState))
				addBufferBarrier(oldVertexBufferState, newBufferState, drawIndexedCommand.vertexBuffer);
			oldVertexBufferState = newBufferState;

			auto& oldIndexBufferState = getBufferState(drawIndexedCommand.indexBuffer);
			oldPipelineStage |= oldIndexBufferState.stage;

			newBufferState.access |= (uint32)vk::AccessFlagBits::eIndexRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;

			if (!isSameState(oldIndexBufferState, newBufferState))
				addBufferBarrier(oldIndexBufferState, newBufferState, drawIndexedCommand.indexBuffer);
			oldIndexBufferState = newBufferState;
		}
	}
}

//**********************************************************************************************************************
static bool findLastSubpassInput(const vector<Framebuffer::Subpass>& subpasses,
	ID<ImageView> colorAttachment, ShaderStage& shaderStages)
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
	const auto& colorAttachments = framebuffer->getColorAttachments();
	auto colorAttachmentCount = (uint32)colorAttachments.size();
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
		auto imageView = vulkanAPI->imageViewPool.get(colorAttachment.imageView);
		auto& oldImageState = getImageState(imageView->getImage(), 
			imageView->getBaseMip(), imageView->getBaseLayer());
		oldPipelineStage |= oldImageState.stage;

		ImageState newImageState;
		newImageState.access = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
		if (colorAttachment.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eColorAttachmentRead;
		newImageState.layout = (uint32)vk::ImageLayout::eColorAttachmentOptimal;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
		newPipelineStage |= newImageState.stage;

		if (!isSameState(oldImageState, newImageState))
			addImageBarrier(oldImageState, newImageState, imageView);
		oldImageState = newImageState;
		
		if (noSubpass)
		{
			vk::AttachmentLoadOp loadOperation;
			vk::ClearValue clearValue;

			if (colorAttachment.clear)
			{
				auto clearColor = clearColors[i];
				array<float, 4> color;
				loadOperation = vk::AttachmentLoadOp::eClear;
				color[0] = clearColor.x; color[1] = clearColor.y;
				color[2] = clearColor.z; color[3] = clearColor.w;
				clearValue = vk::ClearValue(vk::ClearColorValue(color));
			}
			else if (colorAttachment.load)
			{
				loadOperation = vk::AttachmentLoadOp::eLoad;
			}
			else
			{
				loadOperation = vk::AttachmentLoadOp::eDontCare;
			}

			vulkanAPI->colorAttachmentInfos[i] = vk::RenderingAttachmentInfo(
				(VkImageView)ResourceExt::getInstance(**imageView),
				vk::ImageLayout::eColorAttachmentOptimal, {}, VK_NULL_HANDLE, vk::ImageLayout::eUndefined, 
				loadOperation, vk::AttachmentStoreOp::eStore, clearValue);
			// TODO: some how utilize write discarding? (eDontCare)
		}
		else
		{
			auto clearColor = clearColors[i];
			array<float, 4> color;
			color[0] = clearColor.x; color[1] = clearColor.y;
			color[2] = clearColor.z; color[3] = clearColor.w;
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
		auto imageView = vulkanAPI->imageViewPool.get(depthStencilAttachment.imageView);
		auto& oldImageState = getImageState(imageView->getImage(),
			imageView->getBaseMip(), imageView->getBaseLayer());
		oldPipelineStage |= oldImageState.stage;

		ImageState newImageState;
		newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		if (depthStencilAttachment.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentRead;
		newImageState.layout = (uint32)vk::ImageLayout::eDepthStencilAttachmentOptimal;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eEarlyFragmentTests;
		newPipelineStage |= newImageState.stage;

		vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		auto imageFormat = imageView->getFormat();

		if (isFormatDepthOnly(imageFormat))
		{
			newImageState.layout = (uint32)vk::ImageLayout::eDepthAttachmentOptimal;
			aspectFlags = vk::ImageAspectFlagBits::eDepth;

			if (noSubpass)
			{
				if (depthStencilAttachment.clear) // TODO: support separated depth/stencil clear?
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
					depthAttachmentInfo.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(
						command.clearDepth, command.clearStencil));
				}
				else if (depthStencilAttachment.load)
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
				}
				else
				{
					depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eDontCare;
				}

				depthAttachmentInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
				depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
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
			aspectFlags = vk::ImageAspectFlagBits::eStencil;

			if (noSubpass)
			{
				if (depthStencilAttachment.clear)
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
					stencilAttachmentInfo.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(
						command.clearDepth, command.clearStencil));
				}
				else if (depthStencilAttachment.load)
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
				}
				else
				{
					stencilAttachmentInfo.loadOp = vk::AttachmentLoadOp::eDontCare;
				}

				stencilAttachmentInfo.imageView = (VkImageView)ResourceExt::getInstance(**imageView);
				stencilAttachmentInfo.imageLayout = vk::ImageLayout::eStencilAttachmentOptimal;
				stencilAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
				stencilAttachmentInfoPtr = &stencilAttachmentInfo;
			}
			else
			{
				vulkanAPI->clearValues.emplace_back(vk::ClearDepthStencilValue(
					command.clearDepth, command.clearStencil));
			}
		}

		if (!isSameState(oldImageState, newImageState))
			addImageBarrier(oldImageState, newImageState, imageView, aspectFlags);

		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eLateFragmentTests; // Do not remove!
		oldImageState = newImageState;
	}

	auto offset = (commandBufferData - data) + command.thisSize;
	addRenderPassBarriers(offset, oldPipelineStage, newPipelineStage);
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
			instance.beginRenderingKHR(renderingInfo, vulkanAPI->dynamicLoader);
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
			auto& imageState = getImageState(imageView->getImage(),
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
			auto& imageState = getImageState(imageView->getImage(),
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
		vulkanAPI->currentPipelines[0] = {}; vulkanAPI->currentPipelineTypes[0] = {};
		vulkanAPI->currentVertexBuffers[0] = vulkanAPI->currentIndexBuffers[0] = {};
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
		vulkanAPI->currentPipelines[0] = {}; vulkanAPI->currentPipelineTypes[0] = {};
		vulkanAPI->currentVertexBuffers[0] = vulkanAPI->currentIndexBuffers[0] = {};
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
			instance.endRenderingKHR(VulkanAPI::get()->dynamicLoader);
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
		if (region.extent == 0u)
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
		command.pipelineType != vulkanAPI->currentPipelineTypes[0])
	{
		auto pipelineView = vulkanAPI->getPipelineView(command.pipelineType, command.pipeline);
		auto pipeline = ResourceExt::getInstance(**pipelineView);
		instance.bindPipeline(toVkPipelineBindPoint(command.pipelineType), 
			pipelineView->getVariantCount() > 1 ?
			((VkPipeline*)pipeline)[command.variant] : (VkPipeline)pipeline);
		vulkanAPI->currentPipelines[0] = command.pipeline;
		vulkanAPI->currentPipelineTypes[0] = command.pipelineType;
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
	auto offset = (int64)(commandBufferData - data) - (int64)command.lastSize;
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

	while (offset >= 0)
	{
		auto subCommand = (const Command*)(data + offset);
		GARDEN_ASSERT((uint8)subCommand->type < (uint8)Command::Type::Count);
		if (subCommand->lastSize == 0)
			break;

		auto commandType = subCommand->type;
		if (commandType == Command::Type::Dispatch || commandType == Command::Type::BindPipeline)
			break;

		offset -= subCommand->lastSize;
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

	auto bufferView = VulkanAPI::get()->bufferPool.get(command.buffer);
	auto vkBuffer = (VkBuffer)ResourceExt::getInstance(**bufferView);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	BufferState newBufferState;
	newBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newBufferState.stage = newPipelineStage;

	auto& oldBufferState = getBufferState(command.buffer);
	oldPipelineStage |= oldBufferState.stage;

	if (!isSameState(oldBufferState, newBufferState))
	{
		addBufferBarrier(oldBufferState, newBufferState, vkBuffer, 
			command.size == 0 ? VK_WHOLE_SIZE : command.size, command.offset);
	}
	oldBufferState = newBufferState;

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

	BufferState newSrcBufferState;
	newSrcBufferState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcBufferState.stage = newPipelineStage;

	BufferState newDstBufferState;
	newDstBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstBufferState.stage = newPipelineStage;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		vulkanAPI->bufferCopies[i] = vk::BufferCopy(region.srcOffset, region.dstOffset,
			region.size == 0 ? srcBuffer->getBinarySize() : region.size);

		auto& oldSrcBufferState = getBufferState(command.source);
		oldPipelineStage |= oldSrcBufferState.stage;

		if (!isSameState(oldSrcBufferState, newSrcBufferState))
			addBufferBarrier(oldSrcBufferState, newSrcBufferState, vkSrcBuffer, VK_WHOLE_SIZE);
		oldSrcBufferState = newSrcBufferState;

		auto& oldDstBufferState = getBufferState(command.destination);
		oldPipelineStage |= oldDstBufferState.stage;

		if (!isSameState(oldDstBufferState, newDstBufferState))
			addBufferBarrier(oldDstBufferState, newDstBufferState, vkDstBuffer, VK_WHOLE_SIZE);
		oldDstBufferState = newDstBufferState;
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

	ImageState newImageState;
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

		// TODO: possibly somehow combine these barriers?
		for (uint32 mip = 0; mip < mipCount; mip++)
		{
			for (uint32 layer = 0; layer < layerCount; layer++)
			{
				auto& oldImageState = getImageState(command.image, 
					region.baseMip + mip, region.baseLayer + layer);
				oldPipelineStage |= oldImageState.stage;
				
				if (!isSameState(oldImageState, newImageState))
				{
					addImageBarrier(oldImageState, newImageState, vkImage,
						region.baseMip + mip, 1, region.baseLayer + layer, 1, aspectFlags);
				}
				oldImageState = newImageState;
			}
		}
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

	ImageState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = newPipelineStage;

	ImageState newDstImageState;
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

		if (region.extent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(srcImage->getSize(), region.srcMipLevel);
			extent = vk::Extent3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			extent = vk::Extent3D(region.extent.x, region.extent.y, region.extent.z);
		}

		vulkanAPI->imageCopies[i] = vk::ImageCopy(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);

		// TODO: possibly somehow combine these barriers?
		for (uint32 j = 0; j < srcSubresource.layerCount; j++)
		{
			auto& oldSrcImageState = getImageState(command.source, 
				region.srcMipLevel, region.srcBaseLayer + j);
			oldPipelineStage |= oldSrcImageState.stage;

			if (!isSameState(oldSrcImageState, newSrcImageState))
			{
				addImageBarrier(oldSrcImageState, newSrcImageState, vkSrcImage, 
					region.srcMipLevel, 1, region.srcBaseLayer + j, 1, srcAspectFlags);
			}
			oldSrcImageState = newSrcImageState;

			auto& oldDstImageState = getImageState(command.destination, 
				region.dstMipLevel, region.dstBaseLayer + j);
			oldPipelineStage |= oldDstImageState.stage;

			if (!isSameState(oldDstImageState, newDstImageState))
			{
				addImageBarrier(oldDstImageState, newDstImageState, vkDstImage,
					region.dstMipLevel, 1, region.dstBaseLayer + j, 1, dstAspectFlags);
			}
			oldDstImageState = newDstImageState;
		}
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

	BufferState newBufferState;
	newBufferState.access = command.toBuffer ?
		(uint32)vk::AccessFlagBits::eTransferWrite : (uint32)vk::AccessFlagBits::eTransferRead;
	newBufferState.stage = newPipelineStage;

	ImageState newImageState;
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
		
		if (region.imageExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(image->getSize(), region.imageMipLevel);
			dstExtent = vk::Extent3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			dstExtent = vk::Extent3D(region.imageExtent.x, region.imageExtent.y, region.imageExtent.z);
		}

		vulkanAPI->bufferImageCopies[i] = vk::BufferImageCopy((vk::DeviceSize)region.bufferOffset,
			region.bufferRowLength, region.bufferImageHeight, imageSubresource, dstOffset, dstExtent);

		auto& oldBufferState = getBufferState(command.buffer);
		oldPipelineStage |= oldBufferState.stage;

		if (!isSameState(oldBufferState, newBufferState))
			addBufferBarrier(oldBufferState, newBufferState, vkBuffer, VK_WHOLE_SIZE);
		oldBufferState = newBufferState;

		for (uint32 j = 0; j < imageSubresource.layerCount; j++)
		{
			auto& oldImageState = getImageState(command.image, 
				region.imageMipLevel, region.imageBaseLayer + j);
			oldPipelineStage |= oldImageState.stage;

			if (!isSameState(oldImageState, newImageState))
			{
				addImageBarrier(oldImageState, newImageState, vkImage,
					region.imageMipLevel, 1, region.imageBaseLayer + j, 1, aspectFlags);
			}
			oldImageState = newImageState;
		}
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

	ImageState newSrcImageState;
	newSrcImageState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcImageState.layout = (uint32)vk::ImageLayout::eTransferSrcOptimal;
	newSrcImageState.stage = newPipelineStage;

	ImageState newDstImageState;
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

		if (region.srcExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(srcImage->getSize(), region.srcMipLevel);
			srcBounds[1] = vk::Offset3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			srcBounds[1] = vk::Offset3D(region.srcExtent.x, region.srcExtent.y, region.srcExtent.z);
		}
		if (region.dstExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(dstImage->getSize(), region.dstMipLevel);
			dstBounds[1] = vk::Offset3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			dstBounds[1] = vk::Offset3D(region.dstExtent.x, region.dstExtent.y, region.dstExtent.z);
		}

		vulkanAPI->imageBlits[i] = vk::ImageBlit(srcSubresource, srcBounds, dstSubresource, dstBounds);

		// TODO: possibly somehow combine these barriers?
		for (uint32 j = 0; j < srcSubresource.layerCount; j++)
		{
			auto& oldSrcImageState = getImageState(command.source, 
				region.srcMipLevel, region.srcBaseLayer + j);
			oldPipelineStage |= oldSrcImageState.stage;

			if (!isSameState(oldSrcImageState, newSrcImageState))
			{
				addImageBarrier(oldSrcImageState, newSrcImageState, vkSrcImage,
					region.srcMipLevel, 1, region.srcBaseLayer + j, 1, srcAspectFlags);
			}
			oldSrcImageState = newSrcImageState;

			auto& oldDstImageState = getImageState(command.destination, 
				region.dstMipLevel, region.dstBaseLayer + j);
			oldPipelineStage |= oldDstImageState.stage;

			if (!isSameState(oldDstImageState, newDstImageState))
			{
				addImageBarrier(oldDstImageState, newDstImageState, vkDstImage,
					region.dstMipLevel, 1, region.dstBaseLayer + j, 1, dstAspectFlags);
			}
			oldDstImageState = newDstImageState;
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	instance.blitImage(vkSrcImage, vk::ImageLayout::eTransferSrcOptimal,
		vkDstImage, vk::ImageLayout::eTransferDstOptimal, command.regionCount, 
		vulkanAPI->imageBlits.data(), (vk::Filter)command.filter);
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void VulkanCommandBuffer::processCommand(const BeginLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("BeginLabel Command Process");

	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	auto floatColor = (float4)command.color;
	array<float, 4> values = { floatColor.x, floatColor.y, floatColor.z, floatColor.w };
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	instance.beginDebugUtilsLabelEXT(debugLabel, VulkanAPI::get()->dynamicLoader);
}
void VulkanCommandBuffer::processCommand(const EndLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("EndLabel Command Process");
	instance.endDebugUtilsLabelEXT(VulkanAPI::get()->dynamicLoader);
}
void VulkanCommandBuffer::processCommand(const InsertLabelCommand& command)
{
	SET_CPU_ZONE_SCOPED("InsertLabel Command Process");

	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	auto floatColor = (float4)command.color;
	array<float, 4> values = { floatColor.x, floatColor.y, floatColor.z, floatColor.w };
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	instance.beginDebugUtilsLabelEXT(debugLabel, VulkanAPI::get()->dynamicLoader);
}
#endif