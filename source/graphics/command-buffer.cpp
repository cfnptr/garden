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

#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/imgui-impl.hpp"

using namespace math;
using namespace garden::graphics;

// TODO: separate big functions into smaller ones.

//**********************************************************************************************************************
static vector<vk::ImageMemoryBarrier> imageMemoryBarriers;
static vector<vk::BufferMemoryBarrier> bufferMemoryBarriers;

static vk::CommandBuffer createVkCommandBuffer(vk::Device device, vk::CommandPool commandPool)
{
	vk::CommandBufferAllocateInfo commandBufferInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::CommandBuffer commandBuffer;
	auto allocateResult = device.allocateCommandBuffers(&commandBufferInfo, &commandBuffer);
	vk::detail::resultCheck(allocateResult, "vk::Device::allocateCommandBuffers");
	return commandBuffer;
}
static vk::Fence createVkFence(vk::Device device, bool isSignaled = false)
{
	vk::FenceCreateInfo fenceInfo(isSignaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags());
	return device.createFence(fenceInfo);
}

//**********************************************************************************************************************
void CommandBuffer::initialize(CommandBufferType type)
{
	switch (type)
	{
	case CommandBufferType::Graphics:
		instance = createVkCommandBuffer(Vulkan::device, Vulkan::graphicsCommandPool); break;
	case CommandBufferType::TransferOnly:
		instance = createVkCommandBuffer(Vulkan::device, Vulkan::transferCommandPool); break;
	case CommandBufferType::ComputeOnly:
		instance = createVkCommandBuffer(Vulkan::device, Vulkan::computeCommandPool); break;
	case CommandBufferType::Frame: break;
	default: abort();
	}

	#if GARDEN_DEBUG
	if (Vulkan::hasDebugUtils)
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
			Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
		}
	}
	#endif

	if (instance)
		fence = createVkFence(Vulkan::device);

	data = malloc<uint8>(capacity);
	this->type = type;
}
void CommandBuffer::terminate()
{
	if (type != CommandBufferType::Frame)
	{
		flushLockedResources(lockedResources);
		flushLockedResources(lockingResources);
	}
	Vulkan::device.destroyFence((VkFence)fence);
	free(data);
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

static bool isSameState(const CommandBuffer::ImageState& oldState, const CommandBuffer::ImageState& newState)
{
	return !(oldState.layout != newState.layout || (oldState.access & writeAccessMask));
}
static bool isSameState(const CommandBuffer::BufferState& oldState, const CommandBuffer::BufferState& newState)
{
	return !(oldState.access & writeAccessMask);
}

//**********************************************************************************************************************
CommandBuffer::ImageState& CommandBuffer::getImageState(ID<Image> image, uint32 mip, uint32 layer)
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
		auto imageView = GraphicsAPI::imagePool.get(image);
		ImageState newImageState;
		newImageState.access = (uint32)VK_ACCESS_NONE;
		newImageState.layout = imageView->layouts[mip * imageView->layerCount + layer];
		newImageState.stage = imageView->swapchain ? 
			(uint32)vk::PipelineStageFlagBits::eBottomOfPipe : (uint32)vk::PipelineStageFlagBits::eNone;
		auto addResult = imageStates.emplace(imageSubresource, newImageState);
		return addResult.first->second;
	}
}

CommandBuffer::BufferState& CommandBuffer::getBufferState(ID<Buffer> buffer)
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
Command* CommandBuffer::allocateCommand(uint32 size)
{
	if (this->size + size > capacity)
	{
		capacity = this->size + size;
		data = realloc<uint8>(data, capacity);
	}

	auto allocation = (Command*)(data + this->size);
	this->size += size;
	return allocation;
}

// also add to the shaders noncoherent tag to skip sync if different buffer or image parts.

//**********************************************************************************************************************
void CommandBuffer::addDescriptorSetBarriers(const DescriptorSet::Range* descriptorSetRange,
	uint32 rangeCount, uint32& oldStage, uint32& newStage)
{
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

	for (uint8 i = 0; i < rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		auto descriptorSet = GraphicsAPI::descriptorSetPool.get(descriptor.set);
		const auto& descriptorSetUniforms = descriptorSet->uniforms;
		auto pipelineType = descriptorSet->pipelineType;

		const map<string, Pipeline::Uniform>* pipelineUniforms = nullptr;
		if (pipelineType == PipelineType::Graphics)
		{
			auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(descriptorSet->pipeline));
			pipelineUniforms = &pipelineView->uniforms;
		}
		else if (pipelineType == PipelineType::Compute)
		{
			auto pipelineView = GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(descriptorSet->pipeline));
			pipelineUniforms = &pipelineView->uniforms;
		}
		else abort();

		for (const auto& pipelineUniform : *pipelineUniforms)
		{
			auto uniform = pipelineUniform.second;
			if (uniform.descriptorSetIndex != i)
				continue;

			auto setCount = descriptor.offset + descriptor.count;
			const auto& descriptorSetUniform = descriptorSetUniforms.at(pipelineUniform.first);

			if (isSamplerType(uniform.type) || isImageType(uniform.type))
			{
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

						auto imageView = GraphicsAPI::imageViewPool.get(ID<ImageView>(resourceArray[k]));
						auto instance = imageView->image;
						auto image = GraphicsAPI::imagePool.get(instance);
						auto imageInstance = (VkImage)image->instance;
						auto imageAspectFlags = toVkImageAspectFlags(imageView->format);
						auto baseMip = imageView->baseMip;
						auto mipCount = baseMip + imageView->mipCount;
						auto baseLayer = imageView->baseLayer;
						auto layerCount = baseLayer + imageView->layerCount;
						newPipelineStage |= newImageState.stage;

						for (uint8 mip = baseMip; mip < mipCount; mip++)
						{
							for (uint32 layer = baseLayer; layer < layerCount; layer++)
							{
								auto& oldImageState = getImageState(instance, mip, layer);
								oldPipelineStage |= oldImageState.stage;

								if (!isSameState(oldImageState, newImageState))
								{
									// TODO: we can transfer only required for rendering mips and layers,
									//		 instead of all image data like this.
									vk::ImageMemoryBarrier imageMemoryBarrier(
										vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
										(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
										VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
										imageInstance, vk::ImageSubresourceRange(imageAspectFlags, mip, 1, layer, 1));
									imageMemoryBarriers.push_back(imageMemoryBarrier);
								}
								oldImageState = newImageState;
							}
						}
					}
				}
			}
			else if (isBufferType(uniform.type))
			{
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
						{
							auto bufferView = GraphicsAPI::bufferPool.get(buffer);
							vk::BufferMemoryBarrier bufferMemoryBarrier(
								vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
								VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
								(VkBuffer)bufferView->instance, 0, bufferView->binarySize);
							// TODO: we can specify only required buffer range.
							bufferMemoryBarriers.push_back(bufferMemoryBarrier);
						}
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
void CommandBuffer::processPipelineBarriers(uint32 oldStage, uint32 newStage)
{
	if (imageMemoryBarriers.empty() && bufferMemoryBarriers.empty())
		return;

	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto oldPipelineStage = (vk::PipelineStageFlagBits)oldStage;
	auto newPipelineStage = (vk::PipelineStageFlagBits)newStage;

	if (oldPipelineStage == vk::PipelineStageFlagBits::eNone)
		oldPipelineStage = vk::PipelineStageFlagBits::eTopOfPipe;

	// TODO: combine overlapping barriers.
	// TODO: use dependency flags

	commandBuffer.pipelineBarrier(oldPipelineStage, newPipelineStage, {}, 0, nullptr,
		(uint32)bufferMemoryBarriers.size(), bufferMemoryBarriers.data(),
		(uint32)imageMemoryBarriers.size(), imageMemoryBarriers.data());

	imageMemoryBarriers.clear();
	bufferMemoryBarriers.clear();
}

//**********************************************************************************************************************
void CommandBuffer::flushLockedResources(vector<CommandBuffer::LockResource>& lockedResources)
{
	for (const auto& pair : lockedResources)
	{
		switch (pair.second)
		{
		case ResourceType::Buffer:
			GraphicsAPI::bufferPool.get(ID<Buffer>(pair.first))->readyLock--;
			break;
		case ResourceType::Image:
			GraphicsAPI::imagePool.get(ID<Image>(pair.first))->readyLock--;
			break;
		case ResourceType::ImageView:
			GraphicsAPI::imageViewPool.get(ID<ImageView>(pair.first))->readyLock--;
			break;
		case ResourceType::Framebuffer:
			GraphicsAPI::framebufferPool.get(ID<Framebuffer>(pair.first))->readyLock--;
			break;
		case ResourceType::GraphicsPipeline:
			GraphicsAPI::graphicsPipelinePool.get(ID<GraphicsPipeline>(pair.first))->readyLock--;
			break;
		case ResourceType::ComputePipeline:
			GraphicsAPI::computePipelinePool.get(ID<ComputePipeline>(pair.first))->readyLock--;
			break;
		case ResourceType::DescriptorSet:
			GraphicsAPI::descriptorSetPool.get(ID<DescriptorSet>(pair.first))->readyLock--;
			break;
		default: abort();
		}
	}
	lockedResources.clear();
}

//**********************************************************************************************************************
void CommandBuffer::submit(uint64 frameIndex)
{
	auto swapchainBuffer = (Swapchain::Buffer*)&Vulkan::swapchain.getCurrentBuffer();
	vk::CommandBuffer commandBuffer; vk::Queue queue;

	if (type == CommandBufferType::Frame)
	{
		instance = commandBuffer = vk::CommandBuffer((VkCommandBuffer)swapchainBuffer->primaryCommandBuffer);
	}
	else
	{
		if (isRunning)
		{
			// TODO: this approach involves stall bubbles.
			// buffer can be busy at the frame start and became free at middle,
			// when we are vsync locked already. Suboptimal.

			vk::Fence fence((VkFence)this->fence);
			auto status = Vulkan::device.getFenceStatus(fence);
			if (status == vk::Result::eNotReady)
				return;
			
			Vulkan::device.resetFences(fence);
			flushLockedResources(lockedResources);
			isRunning = false;
		}

		if (!hasAnyCommand)
		{
			size = lastSize = 0;
			return;
		}

		if (type == CommandBufferType::TransferOnly)
			queue = Vulkan::transferQueue;
		else if (type == CommandBufferType::ComputeOnly)
			queue = Vulkan::computeQueue;
		else
			queue = Vulkan::graphicsQueue;
		commandBuffer = vk::CommandBuffer((VkCommandBuffer)instance);
	}

	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	commandBuffer.begin(beginInfo);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	this->frameIndex = frameIndex;

	if (type == CommandBufferType::Frame && GraphicsAPI::recordGpuTime)
	{
		commandBuffer.resetQueryPool(swapchainBuffer->queryPool, 0, 2);
		commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, swapchainBuffer->queryPool, 0);
		swapchainBuffer->isPoolClean = true;
		// TODO: record transfer and compute command buffer perf.
	}
	#endif

	psize offset = 0;
	while (offset < size)
	{
		auto command = (const Command*)(data + offset);

		switch (command->type)
		{
		case Command::Type::BeginRenderPass:
			processCommand(*(const BeginRenderPassCommand*)command); break;
		case Command::Type::NextSubpass:
			processCommand(*(const NextSubpassCommand*)command); break;
		case Command::Type::Execute:
			processCommand(*(const ExecuteCommand*)command); break;
		case Command::Type::EndRenderPass:
			processCommand(*(const EndRenderPassCommand*)command); break;
		case Command::Type::ClearAttachments:
			processCommand(*(const ClearAttachmentsCommand*)command); break;
		case Command::Type::BindPipeline:
			processCommand(*(const BindPipelineCommand*)command); break;
		case Command::Type::BindDescriptorSets:
			processCommand(*(const BindDescriptorSetsCommand*)command); break;
		case Command::Type::PushConstants:
			processCommand(*(const PushConstantsCommand*)command); break;
		case Command::Type::SetViewport:
			processCommand(*(const SetViewportCommand*)command); break;
		case Command::Type::SetScissor:
			processCommand(*(const SetScissorCommand*)command); break;
		case Command::Type::SetViewportScissor:
			processCommand(*(const SetViewportScissorCommand*)command); break;
		case Command::Type::Draw:
			processCommand(*(const DrawCommand*)command); break;
		case Command::Type::DrawIndexed:
			processCommand(*(const DrawIndexedCommand*)command); break;
		case Command::Type::Dispatch:
			processCommand(*(const DispatchCommand*)command); break;
		case Command::Type::FillBuffer:
			processCommand(*(const FillBufferCommand*)command); break;
		case Command::Type::CopyBuffer:
			processCommand(*(const CopyBufferCommand*)command); break;
		case Command::Type::ClearImage:
			processCommand(*(const ClearImageCommand*)command); break;
		case Command::Type::CopyImage:
			processCommand(*(const CopyImageCommand*)command); break;
		case Command::Type::CopyBufferImage:
			processCommand(*(const CopyBufferImageCommand*)command); break;
		case Command::Type::BlitImage:
			processCommand(*(const BlitImageCommand*)command); break;

		#if GARDEN_DEBUG
		case Command::Type::BeginLabel:
			processCommand(*(const BeginLabelCommand*)command); break;
		case Command::Type::EndLabel:
			processCommand(*(const EndLabelCommand*)command); break;
		case Command::Type::InsertLabel:
			processCommand(*(const InsertLabelCommand*)command); break;
		#endif

		default: abort();
		}

		offset += command->thisSize;
	}

	GARDEN_ASSERT(offset == size);

	if (type == CommandBufferType::Frame)
	{
		vk::Image swapchainImage((VkImage)GraphicsAPI::imagePool.get(swapchainBuffer->colorImage)->instance);
		if (!hasAnyCommand)
		{
			auto& oldImageState = getImageState(swapchainBuffer->colorImage, 0, 0);
			ImageState newImageState;
			newImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
			newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
			newImageState.stage = (uint32)vk::PipelineStageFlagBits::eTransfer;

			vk::ImageMemoryBarrier imageMemoryBarrier(
				vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				swapchainImage, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
				vk::PipelineStageFlags(newImageState.stage), {}, {}, {}, imageMemoryBarrier);
			oldImageState = newImageState;

			array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
			commandBuffer.clearColorImage(swapchainImage,
				vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(clearColor),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		}

		#if GARDEN_DEBUG || GARDEN_EDITOR
		{
			auto& oldImageState = getImageState(swapchainBuffer->colorImage, 0, 0);
			ImageState newImageState;
			// We adding read barrier because we loading buffer pixels.
			newImageState.access = (uint32)(vk::AccessFlagBits::eColorAttachmentRead |
				vk::AccessFlagBits::eColorAttachmentWrite);
			newImageState.layout = (uint32)vk::ImageLayout::eColorAttachmentOptimal;
			newImageState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;

			if (!isSameState(oldImageState, newImageState))
			{
				auto oldPipelineStage = (vk::PipelineStageFlagBits)oldImageState.stage;
				if (oldPipelineStage == vk::PipelineStageFlagBits::eNone)
					oldPipelineStage = vk::PipelineStageFlagBits::eTopOfPipe;
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
					(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					swapchainImage, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
				commandBuffer.pipelineBarrier(oldPipelineStage,
					vk::PipelineStageFlags(newImageState.stage), {}, {}, {}, imageMemoryBarrier);
				newImageState.layout = (uint32)vk::ImageLayout::eGeneral;
			}
			oldImageState = newImageState;

			auto drawData = ImGui::GetDrawData();
			auto framebufferSize = Vulkan::swapchain.getFramebufferSize();
			auto extent = vk::Extent2D((uint32)framebufferSize.x, (uint32)framebufferSize.y);
			vk::RenderPassBeginInfo beginInfo(ImGuiData::renderPass,
				ImGuiData::framebuffers[Vulkan::swapchain.getCurrentBufferIndex()], vk::Rect2D({}, extent));
			commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
			ImGui_ImplVulkan_RenderDrawData(drawData, (VkCommandBuffer)commandBuffer);
			commandBuffer.endRenderPass();
		}
		#endif

		auto& oldImageState = getImageState(swapchainBuffer->colorImage, 0, 0);
		ImageState newImageState;
		newImageState.access = (uint32)vk::AccessFlagBits::eNone;
		newImageState.layout = (uint32)vk::ImageLayout::ePresentSrcKHR;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eBottomOfPipe;
		vk::ImageMemoryBarrier imageMemoryBarrier(
			vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
			(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			swapchainImage, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		commandBuffer.pipelineBarrier(oldImageState.stage == (uint32)vk::PipelineStageFlagBits::eNone ?
			vk::PipelineStageFlagBits::eTopOfPipe : (vk::PipelineStageFlagBits)oldImageState.stage,
			vk::PipelineStageFlags(newImageState.stage), {}, {}, {}, imageMemoryBarrier);
		oldImageState = newImageState;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (GraphicsAPI::recordGpuTime)
			commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, swapchainBuffer->queryPool, 1);
		#endif
	}

	commandBuffer.end();

	if (type == CommandBufferType::Frame)
	{
		Vulkan::swapchain.submit();
	}
	else
	{
		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		queue.submit(submitInfo, (VkFence)fence);
	}
	
	for (auto pair : imageStates)
	{
		auto imageState = pair.first;
		auto image = GraphicsAPI::imagePool.get(imageState.image);
		image->layouts[imageState.mip * image->layerCount + imageState.layer] = pair.second.layout;
	}

	imageStates.clear();
	std::swap(lockingResources, lockedResources);
	size = lastSize = 0;
	hasAnyCommand = false;
	isRunning = true;
}

//**********************************************************************************************************************
static vector<vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
static vector<vk::ClearValue> clearValues;

void CommandBuffer::processCommand(const BeginRenderPassCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto framebuffer = GraphicsAPI::framebufferPool.get(command.framebuffer);
	const auto& colorAttachments = framebuffer->colorAttachments;
	auto colorAttachmentCount = (uint32)colorAttachments.size();
	auto commandBufferData = (const uint8*)&command;
	auto clearColors = (const float4*)(commandBufferData + sizeof(BeginRenderPassCommandBase));
	uint32 oldPipelineStage = 0, newPipelineStage = 0;

	noSubpass = framebuffer->subpasses.empty();
	if (noSubpass)
	{
		if (colorAttachmentInfos.size() < colorAttachmentCount)
			colorAttachmentInfos.resize(colorAttachmentCount);
	}

	for (uint32 i = 0; i < colorAttachmentCount; i++)
	{
		auto colorAttachment = colorAttachments[i];
		auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
		auto& oldImageState = getImageState(imageView->image, imageView->baseMip, imageView->baseLayer);
		oldPipelineStage |= oldImageState.stage;

		ImageState newImageState;
		newImageState.access = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
		if (colorAttachment.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eColorAttachmentRead;
		newImageState.layout = (uint32)vk::ImageLayout::eColorAttachmentOptimal;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
		newPipelineStage |= newImageState.stage;

		if (!isSameState(oldImageState, newImageState))
		{
			auto image = GraphicsAPI::imagePool.get(imageView->image);
			vk::ImageMemoryBarrier imageMemoryBarrier(
				vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, (VkImage)image->instance,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
					imageView->baseMip, imageView->mipCount, imageView->baseLayer, imageView->layerCount));
			imageMemoryBarriers.push_back(imageMemoryBarrier);
		}
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

			colorAttachmentInfos[i] = vk::RenderingAttachmentInfo((VkImageView)imageView->instance,
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
			clearValues.emplace_back(vk::ClearColorValue(color));
		}
	}
	
	vk::RenderingAttachmentInfoKHR depthAttachmentInfo;
	vk::RenderingAttachmentInfoKHR stencilAttachmentInfo;
	vk::RenderingAttachmentInfoKHR* depthAttachmentInfoPtr = nullptr;
	vk::RenderingAttachmentInfoKHR* stencilAttachmentInfoPtr = nullptr;

	if (framebuffer->depthStencilAttachment.imageView)
	{
		auto depthStencilAttachment = framebuffer->depthStencilAttachment;
		auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
		auto& oldImageState = getImageState(imageView->image, imageView->baseMip, imageView->baseLayer);
		oldPipelineStage |= oldImageState.stage;

		ImageState newImageState;
		newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		if (depthStencilAttachment.load)
			newImageState.access |= (uint32)vk::AccessFlagBits::eDepthStencilAttachmentRead;
		newImageState.layout = (uint32)vk::ImageLayout::eDepthStencilAttachmentOptimal;
		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eEarlyFragmentTests;
		newPipelineStage |= newImageState.stage;

		vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		auto imageFormat = imageView->format;

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

				depthAttachmentInfo.imageView = (VkImageView)imageView->instance;
				depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
				depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
				depthAttachmentInfoPtr = &depthAttachmentInfo;
			}
			else
			{
				clearValues.emplace_back(vk::ClearDepthStencilValue(command.clearDepth, command.clearStencil));
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

				stencilAttachmentInfo.imageView = (VkImageView)imageView->instance;
				stencilAttachmentInfo.imageLayout = vk::ImageLayout::eStencilAttachmentOptimal;
				stencilAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
				stencilAttachmentInfoPtr = &stencilAttachmentInfo;
			}
			else
			{
				clearValues.emplace_back(vk::ClearDepthStencilValue(command.clearDepth, command.clearStencil));
			}
		}

		if (!isSameState(oldImageState, newImageState))
		{
			auto image = GraphicsAPI::imagePool.get(imageView->image);
			vk::ImageMemoryBarrier imageMemoryBarrier(
				vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
				(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				(VkImage)image->instance, vk::ImageSubresourceRange(aspectFlags, 
					imageView->baseMip, imageView->mipCount, imageView->baseLayer, imageView->layerCount));
			imageMemoryBarriers.push_back(imageMemoryBarrier);
		}

		newImageState.stage = (uint32)vk::PipelineStageFlagBits::eLateFragmentTests; // Do not remove!
		oldImageState = newImageState;
	}

	auto offset = (commandBufferData - data) + command.thisSize;

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
			{
				auto bufferView = GraphicsAPI::bufferPool.get(drawCommand.vertexBuffer);
				vk::BufferMemoryBarrier bufferMemoryBarrier(
					vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkBuffer)bufferView->instance, 0, bufferView->binarySize);
				// TODO: we can specify only required buffer range.
				bufferMemoryBarriers.push_back(bufferMemoryBarrier);
			}
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
			{
				auto bufferView = GraphicsAPI::bufferPool.get(drawIndexedCommand.vertexBuffer);
				vk::BufferMemoryBarrier bufferMemoryBarrier(
					vk::AccessFlags(oldVertexBufferState.access), vk::AccessFlags(newBufferState.access),
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkBuffer)bufferView->instance, 0, bufferView->binarySize);
				// TODO: we can specify only required buffer range.
				bufferMemoryBarriers.push_back(bufferMemoryBarrier);
			}
			oldVertexBufferState = newBufferState;

			auto& oldIndexBufferState = getBufferState(drawIndexedCommand.indexBuffer);
			oldPipelineStage |= oldIndexBufferState.stage;

			newBufferState.access |= (uint32)vk::AccessFlagBits::eIndexRead;
			newBufferState.stage = (uint32)vk::PipelineStageFlagBits::eVertexInput;
			newPipelineStage |= newBufferState.stage;

			if (!isSameState(oldIndexBufferState, newBufferState))
			{
				auto bufferView = GraphicsAPI::bufferPool.get(drawIndexedCommand.vertexBuffer);
				vk::BufferMemoryBarrier bufferMemoryBarrier(
					vk::AccessFlags(oldIndexBufferState.access), vk::AccessFlags(newBufferState.access),
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkBuffer)bufferView->instance, 0, bufferView->binarySize);
				// TODO: we can specify only required buffer range.
				bufferMemoryBarriers.push_back(bufferMemoryBarrier);
				oldIndexBufferState = newBufferState;
			}
			else
			{
				oldIndexBufferState.stage = newBufferState.stage;
			}
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	vk::Rect2D rect({ command.region.x, command.region.y },
		{ (uint32)command.region.z, (uint32)command.region.w });

	if (noSubpass)
	{
		vk::RenderingInfo renderingInfo(command.asyncRecording ?
			vk::RenderingFlagBits::eContentsSecondaryCommandBuffers : vk::RenderingFlags(),
			rect, 1, 0, colorAttachmentCount, colorAttachmentInfos.data(),
			depthAttachmentInfoPtr, stencilAttachmentInfoPtr);

		if (Vulkan::versionMinor < 3)
			commandBuffer.beginRenderingKHR(renderingInfo, Vulkan::dynamicLoader);
		else 
			commandBuffer.beginRendering(renderingInfo);
	}
	else
	{
		vk::RenderPassBeginInfo beginInfo((VkRenderPass)framebuffer->renderPass,
			(VkFramebuffer)framebuffer->instance, rect, clearValues);
		commandBuffer.beginRenderPass(beginInfo, command.asyncRecording ?
			vk::SubpassContents::eSecondaryCommandBuffers : vk::SubpassContents::eInline);
		clearValues.clear();

		const auto& subpasses = framebuffer->subpasses;
		for (uint32 i = 0; i < colorAttachmentCount; i++)
		{
			auto colorAttachment = colorAttachments[i];
			auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
			auto& imageState = getImageState(imageView->image, imageView->baseMip, imageView->baseLayer);
			imageState.layout = (uint32)vk::ImageLayout::eGeneral;

			auto isLastInput = false;
			ShaderStage shaderStages;

			// TODO: cache this and do not search each frame?
			for (auto subpass = subpasses.rbegin(); subpass != subpasses.rend(); subpass++)
			{
				for (const auto& inputAttachment : subpass->inputAttachments)
				{
					if (colorAttachment.imageView == inputAttachment.imageView)
					{
						isLastInput = true; shaderStages = inputAttachment.shaderStages;
						subpass = subpasses.rend() - 1;
						break;
					}
				}
			}

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

		if (framebuffer->depthStencilAttachment.imageView)
		{
			auto depthStencilAttachment = framebuffer->depthStencilAttachment;
			auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
			auto& imageState = getImageState(imageView->image, imageView->baseMip, imageView->baseLayer);
			imageState.layout = (uint32)vk::ImageLayout::eGeneral;

			auto isLastInput = false;
			ShaderStage shaderStages;

			// TODO: cache this and do not search each frame?
			for (auto subpass = subpasses.rbegin(); subpass != subpasses.rend(); subpass++)
			{
				for (const auto& inputAttachment : subpass->inputAttachments)
				{
					if (depthStencilAttachment.imageView == inputAttachment.imageView)
					{
						isLastInput = true; shaderStages = inputAttachment.shaderStages;
						subpass = subpasses.rend() - 1;
						break;
					}
				}
			}

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
		Framebuffer::currentPipelines[0] = {}; Framebuffer::currentPipelineTypes[0] = {};
		Framebuffer::currentVertexBuffers[0] = Framebuffer::currentIndexBuffers[0] = {};
	}
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const NextSubpassCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	commandBuffer.nextSubpass(command.asyncRecording ?
		vk::SubpassContents::eSecondaryCommandBuffers : vk::SubpassContents::eInline);

	if (!command.asyncRecording)
	{
		Framebuffer::currentPipelines[0] = {}; Framebuffer::currentPipelineTypes[0] = {};
		Framebuffer::currentVertexBuffers[0] = Framebuffer::currentIndexBuffers[0] = {};
	}
}

void CommandBuffer::processCommand(const ExecuteCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	commandBuffer.executeCommands(command.bufferCount, (const vk::CommandBuffer*)
		((const uint8*)&command + sizeof(ExecuteCommandBase)));
}

void CommandBuffer::processCommand(const EndRenderPassCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	if (noSubpass)
	{
		if (Vulkan::versionMinor < 3)
			commandBuffer.endRenderingKHR(Vulkan::dynamicLoader);
		else
			commandBuffer.endRendering();
	}
	else
	{
		commandBuffer.endRenderPass();
	}
}

//**********************************************************************************************************************
static vector<vk::ClearAttachment> clearAttachments;
static vector<vk::ClearRect> clearAttachmentsRects;

void CommandBuffer::processCommand(const ClearAttachmentsCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto framebufferView = GraphicsAPI::framebufferPool.get(command.framebuffer);

	const auto& colorAttachments = framebufferView->colorAttachments;
	auto attachments = (const Framebuffer::ClearAttachment*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase));
	auto regions = (const Framebuffer::ClearRegion*)(
		(const uint8*)&command + sizeof(ClearAttachmentsCommandBase) +
		command.attachmentCount * sizeof(Framebuffer::ClearAttachment));

	if (clearAttachments.size() < command.attachmentCount)
		clearAttachments.resize(command.attachmentCount);
	if (clearAttachmentsRects.size() < command.regionCount)
		clearAttachmentsRects.resize(command.regionCount);

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
			GARDEN_ASSERT(framebufferView->depthStencilAttachment.imageView);
			attachmentView = framebufferView->depthStencilAttachment.imageView;
		}
		else abort();

		auto imageView = GraphicsAPI::imageViewPool.get(attachmentView);
		auto format = imageView->format;
		vk::ClearValue clearValue;

		if (isFormatFloat(format))
		{
			memcpy(clearValue.color.float32.data(), &attachment.color.floatValue, sizeof(float) * 4);
		}
		else if (isFormatInt(format))
		{
			memcpy(clearValue.color.int32.data(), &attachment.color.intValue, sizeof(int32) * 4);
		}
		else if (isFormatUint(format))
		{
			memcpy(clearValue.color.uint32.data(), &attachment.color.uintValue, sizeof(uint32) * 4);
		}
		else
		{
			clearValue.depthStencil.depth = attachment.color.deptStencilValue.depth;
			clearValue.depthStencil.stencil = attachment.color.deptStencilValue.stencil;
		}

		clearAttachments[i] = vk::ClearAttachment(toVkImageAspectFlags(format), attachment.index, clearValue);
	}

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		
		vk::Rect2D rect({ (int32)region.offset.x, (int32)region.offset.y }, {});
		if (region.extent == 0u)
		{
			rect.extent.width = framebufferView->size.x;
			rect.extent.height = framebufferView->size.y;
		}
		else
		{
			rect.extent.width = region.extent.x;
			rect.extent.height = region.extent.y;
		}
		
		clearAttachmentsRects[i] = vk::ClearRect(rect, region.baseLayer,
			region.layerCount == 0 ? 1 : region.layerCount);
	}

	// TODO: should we add barriers? Looks like no.
	commandBuffer.clearAttachments(command.attachmentCount, clearAttachments.data(),
		command.regionCount, clearAttachmentsRects.data());
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const BindPipelineCommand& command)
{	
	if (command.pipeline != Framebuffer::currentPipelines[0] ||
		command.pipelineType != Framebuffer::currentPipelineTypes[0])
	{
		vk::Pipeline pipeline;
		if (command.pipelineType == PipelineType::Graphics)
		{
			auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(ID<GraphicsPipeline>(command.pipeline));
			pipeline = pipelineView->variantCount > 1 ?
				((VkPipeline*)pipelineView->instance)[command.variant] : (VkPipeline)pipelineView->instance;
		}
		else if (command.pipelineType == PipelineType::Compute)
		{
			auto pipelineView = GraphicsAPI::computePipelinePool.get(ID<ComputePipeline>(command.pipeline));
			pipeline = pipelineView->variantCount > 1 ?
				((VkPipeline*)pipelineView->instance)[command.variant] : (VkPipeline)pipelineView->instance;
		}
		else abort();

		vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
		commandBuffer.bindPipeline(toVkPipelineBindPoint(command.pipelineType), pipeline);
		Framebuffer::currentPipelines[0] = command.pipeline;
		Framebuffer::currentPipelineTypes[0] = command.pipelineType;
	}
}

//**********************************************************************************************************************
static vector<vk::DescriptorSet> descriptorSets;

void CommandBuffer::processCommand(const BindDescriptorSetsCommand& command)
{
	if (command.asyncRecording)
		return;

	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);

	auto descriptorSetRange = (const DescriptorSet::Range*)(
		(const uint8*)&command + sizeof(BindDescriptorSetsCommandBase));

	for (uint8 i = 0; i < command.rangeCount; i++)
	{
		auto descriptor = descriptorSetRange[i];
		auto descriptorSet = GraphicsAPI::descriptorSetPool.get(descriptor.set);
		auto instance = (vk::DescriptorSet*)descriptorSet->instance;

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

	auto descriptorSet = GraphicsAPI::descriptorSetPool.get(descriptorSetRange[0].set);
	auto pipelineType = descriptorSet->pipelineType;
	vk::PipelineBindPoint bindPoint; vk::PipelineLayout pipelineLayout;

	if (pipelineType == PipelineType::Graphics)
	{
		bindPoint = vk::PipelineBindPoint::eGraphics;
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(
			ID<GraphicsPipeline>(descriptorSet->pipeline));
		pipelineLayout = (VkPipelineLayout)pipelineView->pipelineLayout;
	}
	else if (pipelineType == PipelineType::Compute)
	{
		bindPoint = vk::PipelineBindPoint::eCompute;
		auto pipelineView = GraphicsAPI::computePipelinePool.get(
			ID<ComputePipeline>(descriptorSet->pipeline));
		pipelineLayout = (VkPipelineLayout)pipelineView->pipelineLayout;
	}
	else abort();

	commandBuffer.bindDescriptorSets(bindPoint, pipelineLayout, 
		0, (uint32)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
	descriptorSets.clear();
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const PushConstantsCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	commandBuffer.pushConstants((VkPipelineLayout)command.pipelineLayout, (vk::ShaderStageFlags)command.shaderStages, 
		0, command.dataSize, (const uint8*)&command + sizeof(PushConstantsCommandBase));
}

void CommandBuffer::processCommand(const SetViewportCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	vk::Viewport viewport(command.viewport.x, command.viewport.y,
		command.viewport.z, command.viewport.w, 0.0f, 1.0f); // TODO: depth
	commandBuffer.setViewport(0, 1, &viewport); // TODO: multiple viewports
}

void CommandBuffer::processCommand(const SetScissorCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	vk::Rect2D scissor({ command.scissor.x, command.scissor.y },
		{ (uint32)command.scissor.z, (uint32)command.scissor.w });
	commandBuffer.setScissor(0, 1, &scissor); // TODO: multiple scissors
}

void CommandBuffer::processCommand(const SetViewportScissorCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	vk::Viewport viewport(command.viewportScissor.x, command.viewportScissor.y,
		command.viewportScissor.z, command.viewportScissor.w, 0.0f, 1.0f);
	vk::Rect2D scissor(
		{ (int32)command.viewportScissor.x, (int32)command.viewportScissor.y },
		{ (uint32)command.viewportScissor.z, (uint32)command.viewportScissor.w });
	commandBuffer.setViewport(0, 1, &viewport); // TODO: multiple viewports
	commandBuffer.setScissor(0, 1, &scissor);
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const DrawCommand& command)
{
	if (command.asyncRecording)
		return;

	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	if (command.vertexBuffer && command.vertexBuffer != Framebuffer::currentVertexBuffers[0])
	{
		const vk::DeviceSize size = 0;
		auto buffer = GraphicsAPI::bufferPool.get(command.vertexBuffer);
		vk::Buffer instance = (VkBuffer)buffer->instance;
		commandBuffer.bindVertexBuffers(0, 1, &instance, &size);
		Framebuffer::currentVertexBuffers[0] = command.vertexBuffer;
	}

	commandBuffer.draw(command.vertexCount, command.instanceCount, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const DrawIndexedCommand& command)
{
	if (command.asyncRecording)
		return;

	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	// TODO: support multiple buffer binding.
	// TODO: add vertex buffer offset support if required.

	if (command.vertexBuffer != Framebuffer::currentVertexBuffers[0])
	{
		const vk::DeviceSize size = 0;
		auto buffer = GraphicsAPI::bufferPool.get(command.vertexBuffer);
		vk::Buffer instance = (VkBuffer)buffer->instance;
		commandBuffer.bindVertexBuffers(0, 1, &instance, &size);
		Framebuffer::currentVertexBuffers[0] = command.vertexBuffer;
	}
	if (command.indexBuffer != Framebuffer::currentIndexBuffers[0])
	{
		auto buffer = GraphicsAPI::bufferPool.get(command.indexBuffer);
		commandBuffer.bindIndexBuffer((VkBuffer)buffer->instance,
			(vk::DeviceSize)(command.indexOffset * toBinarySize(command.indexType)),
			toVkIndexType(command.indexType));
		Framebuffer::currentIndexBuffers[0] = command.indexBuffer;
	}

	commandBuffer.drawIndexed(command.indexCount, command.instanceCount,
		command.indexOffset, command.vertexOffset, command.instanceOffset);
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const DispatchCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
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
	commandBuffer.dispatch(command.groupCount.x, command.groupCount.y, command.groupCount.z);
}

//**********************************************************************************************************************
void CommandBuffer::processCommand(const FillBufferCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto buffer = GraphicsAPI::bufferPool.get(command.buffer);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	BufferState newBufferState;
	newBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newBufferState.stage = newPipelineStage;

	auto& oldBufferState = getBufferState(command.buffer);
	oldPipelineStage |= oldBufferState.stage;

	if (!isSameState(oldBufferState, newBufferState))
	{
		vk::BufferMemoryBarrier bufferMemoryBarrier(
			vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			(VkBuffer)buffer->instance, command.offset,
			command.size == 0 ? buffer->binarySize : command.size);
		bufferMemoryBarriers.push_back(bufferMemoryBarrier);
	}
	oldBufferState = newBufferState;

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	commandBuffer.fillBuffer((VkBuffer)buffer->instance, command.offset,
		command.size == 0 ? VK_WHOLE_SIZE : command.size, command.data);
}

//**********************************************************************************************************************
static vector<vk::BufferCopy> bufferCopies;

void CommandBuffer::processCommand(const CopyBufferCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto srcBuffer = GraphicsAPI::bufferPool.get(command.source);
	auto dstBuffer = GraphicsAPI::bufferPool.get(command.destination);
	auto regions = (const Buffer::CopyRegion*)((const uint8*)&command + sizeof(CopyBufferCommandBase));
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (bufferCopies.size() < command.regionCount)
		bufferCopies.resize(command.regionCount);

	BufferState newSrcBufferState;
	newSrcBufferState.access = (uint32)vk::AccessFlagBits::eTransferRead;
	newSrcBufferState.stage = newPipelineStage;

	BufferState newDstBufferState;
	newDstBufferState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newDstBufferState.stage = newPipelineStage;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		bufferCopies[i] = vk::BufferCopy(region.srcOffset, region.dstOffset,
			region.size == 0 ? srcBuffer->binarySize : region.size);

		auto& oldSrcBufferState = getBufferState(command.source);
		oldPipelineStage |= oldSrcBufferState.stage;

		if (!isSameState(oldSrcBufferState, newSrcBufferState))
		{
			vk::BufferMemoryBarrier bufferMemoryBarrier(
				vk::AccessFlags(oldSrcBufferState.access), vk::AccessFlags(newSrcBufferState.access),
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				(VkBuffer)srcBuffer->instance, 0, srcBuffer->binarySize);
			bufferMemoryBarriers.push_back(bufferMemoryBarrier);
		}
		oldSrcBufferState = newSrcBufferState;

		auto& oldDstBufferState = getBufferState(command.destination);
		oldPipelineStage |= oldDstBufferState.stage;

		if (!isSameState(oldDstBufferState, newDstBufferState))
		{
			vk::BufferMemoryBarrier bufferMemoryBarrier(
				vk::AccessFlags(oldDstBufferState.access), vk::AccessFlags(newDstBufferState.access),
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				(VkBuffer)dstBuffer->instance, 0, dstBuffer->binarySize);
			bufferMemoryBarriers.push_back(bufferMemoryBarrier);
			
		}
		oldDstBufferState = newDstBufferState;
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	commandBuffer.copyBuffer((VkBuffer)srcBuffer->instance,
		(VkBuffer)dstBuffer->instance, command.regionCount, bufferCopies.data());
}

//**********************************************************************************************************************
static vector<vk::ImageSubresourceRange> imageClears;

void CommandBuffer::processCommand(const ClearImageCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto image = GraphicsAPI::imagePool.get(command.image);
	auto regions = (const Image::ClearRegion*)((const uint8*)&command + sizeof(ClearImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(image->format);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (imageClears.size() < command.regionCount)
		imageClears.resize(command.regionCount);

	ImageState newImageState;
	newImageState.access = (uint32)vk::AccessFlagBits::eTransferWrite;
	newImageState.layout = (uint32)vk::ImageLayout::eTransferDstOptimal;
	newImageState.stage = newPipelineStage;

	for (uint32 i = 0; i < command.regionCount; i++)
	{
		auto region = regions[i];
		auto mipCount = region.mipCount == 0 ? image->mipCount : region.mipCount;
		auto layerCount = region.layerCount == 0 ? image->layerCount : region.layerCount;

		imageClears[i] = vk::ImageSubresourceRange(srcAspectFlags,
			region.baseMip, mipCount, region.baseLayer, layerCount);

		// TODO: possibly somehow combine these barriers?
		for (uint32 mip = 0; mip < mipCount; mip++)
		{
			for (uint32 layer = 0; layer < layerCount; layer++)
			{
				auto& oldImageState = getImageState(command.image, region.baseMip + mip, region.baseLayer + layer);
				oldPipelineStage |= oldImageState.stage;

				if (!isSameState(oldImageState, newImageState))
				{
					vk::ImageMemoryBarrier imageMemoryBarrier(
						vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
						(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
						VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
						(VkImage)image->instance, vk::ImageSubresourceRange(
							srcAspectFlags, region.baseMip + mip, 1, region.baseLayer + layer, 1));
					imageMemoryBarriers.push_back(imageMemoryBarrier);
				}
				oldImageState = newImageState;
			}
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	if (isFormatColor(image->format))
	{
		vk::ClearColorValue clearValue;
		if (command.clearType == 1)
			memcpy(clearValue.float32.data(), &command.color, sizeof(float) * 4);
		else if (command.clearType == 2)
			memcpy(clearValue.int32.data(), &command.color, sizeof(int32) * 4);
		else if (command.clearType == 3)
			memcpy(clearValue.uint32.data(), &command.color, sizeof(uint32) * 4);
		else abort();

		commandBuffer.clearColorImage((VkImage)image->instance, vk::ImageLayout::eTransferDstOptimal,
			&clearValue, command.regionCount, imageClears.data());
	}
	else
	{
		vk::ClearDepthStencilValue clearValue;
		clearValue.depth = command.color.x;
		memcpy(&clearValue.stencil, &command.color.y, sizeof(uint32));
		commandBuffer.clearDepthStencilImage((VkImage)image->instance, vk::ImageLayout::eTransferDstOptimal, 
			&clearValue, command.regionCount, imageClears.data());
	}
}

//**********************************************************************************************************************
static vector<vk::ImageCopy> imageCopies;

void CommandBuffer::processCommand(const CopyImageCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto srcImage = GraphicsAPI::imagePool.get(command.source);
	auto dstImage = GraphicsAPI::imagePool.get(command.destination);
	auto regions = (const Image::CopyImageRegion*)((const uint8*)&command + sizeof(CopyImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->format);
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->format);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (imageCopies.size() < command.regionCount)
		imageCopies.resize(command.regionCount);

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
			region.layerCount == 0 ? srcImage->layerCount : region.layerCount;
		vk::Offset3D srcOffset(region.srcOffset.x, region.srcOffset.y, region.srcOffset.z);
		vk::Offset3D dstOffset(region.dstOffset.x, region.dstOffset.y, region.dstOffset.z);
		vk::Extent3D extent;

		if (region.extent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(srcImage->size, region.srcMipLevel);
			extent = vk::Extent3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			extent = vk::Extent3D(region.extent.x, region.extent.y, region.extent.z);
		}

		imageCopies[i] = vk::ImageCopy(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);

		// TODO: possibly somehow combine these barriers?
		for (uint32 j = 0; j < srcSubresource.layerCount; j++)
		{
			auto& oldSrcImageState = getImageState(command.source, region.srcMipLevel, region.srcBaseLayer + j);
			oldPipelineStage |= oldSrcImageState.stage;

			if (!isSameState(oldSrcImageState, newSrcImageState))
			{
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldSrcImageState.access), vk::AccessFlags(newSrcImageState.access),
					(vk::ImageLayout)oldSrcImageState.layout, (vk::ImageLayout)newSrcImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkImage)srcImage->instance, vk::ImageSubresourceRange(
						srcAspectFlags, region.srcMipLevel, 1, region.srcBaseLayer + j, 1));
				imageMemoryBarriers.push_back(imageMemoryBarrier);
			}
			oldSrcImageState = newSrcImageState;

			auto& oldDstImageState = getImageState(command.destination, region.dstMipLevel, region.dstBaseLayer + j);
			oldPipelineStage |= oldDstImageState.stage;

			if (!isSameState(oldDstImageState, newDstImageState))
			{
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldDstImageState.access), vk::AccessFlags(newDstImageState.access),
					(vk::ImageLayout)oldDstImageState.layout, (vk::ImageLayout)newDstImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkImage)dstImage->instance, vk::ImageSubresourceRange(
						dstAspectFlags, region.dstMipLevel, 1, region.dstBaseLayer + j, 1));
				imageMemoryBarriers.push_back(imageMemoryBarrier);
			}
			oldDstImageState = newDstImageState;
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	commandBuffer.copyImage(
		(VkImage)srcImage->instance, vk::ImageLayout::eTransferSrcOptimal,
		(VkImage)dstImage->instance, vk::ImageLayout::eTransferDstOptimal,
		command.regionCount, imageCopies.data());
}

//**********************************************************************************************************************
static vector<vk::BufferImageCopy> bufferImageCopies;

void CommandBuffer::processCommand(const CopyBufferImageCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto buffer = GraphicsAPI::bufferPool.get(command.buffer);
	auto image = GraphicsAPI::imagePool.get(command.image);
	auto regions = (const Image::CopyBufferRegion*)((const uint8*)&command + sizeof(CopyBufferImageCommandBase));
	auto dstAspectFlags = toVkImageAspectFlags(image->format);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (bufferImageCopies.size() < command.regionCount)
		bufferImageCopies.resize(command.regionCount);

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
		vk::ImageSubresourceLayers imageSubresource(dstAspectFlags, region.imageMipLevel, region.imageBaseLayer);
		imageSubresource.layerCount = region.imageLayerCount == 0 ? image->layerCount : region.imageLayerCount;
		vk::Offset3D dstOffset(region.imageOffset.x, region.imageOffset.y, region.imageOffset.z);
		vk::Extent3D dstExtent;
		
		if (region.imageExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(image->size, region.imageMipLevel);
			dstExtent = vk::Extent3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			dstExtent = vk::Extent3D(region.imageExtent.x, region.imageExtent.y, region.imageExtent.z);
		}

		bufferImageCopies[i] = vk::BufferImageCopy((vk::DeviceSize)region.bufferOffset, 
			region.bufferRowLength, region.bufferImageHeight, imageSubresource, dstOffset, dstExtent);

		auto& oldBufferState = getBufferState(command.buffer);
		oldPipelineStage |= oldBufferState.stage;

		if (!isSameState(oldBufferState, newBufferState))
		{
			vk::BufferMemoryBarrier bufferMemoryBarrier(
				vk::AccessFlags(oldBufferState.access), vk::AccessFlags(newBufferState.access),
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				(VkBuffer)buffer->instance, 0, buffer->binarySize);
			bufferMemoryBarriers.push_back(bufferMemoryBarrier);
		}
		oldBufferState = newBufferState;

		for (uint32 j = 0; j < imageSubresource.layerCount; j++)
		{
			auto& oldImageState = getImageState(command.image, region.imageMipLevel, region.imageBaseLayer + j);
			oldPipelineStage |= oldImageState.stage;

			if (!isSameState(oldImageState, newImageState))
			{
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldImageState.access), vk::AccessFlags(newImageState.access),
					(vk::ImageLayout)oldImageState.layout, (vk::ImageLayout)newImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkImage)image->instance, vk::ImageSubresourceRange(
						dstAspectFlags, region.imageMipLevel, 1, region.imageBaseLayer + j, 1));
				imageMemoryBarriers.push_back(imageMemoryBarrier);
			}
			oldImageState = newImageState;
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	if (command.toBuffer)
	{
		commandBuffer.copyImageToBuffer((VkImage)image->instance, vk::ImageLayout::eTransferSrcOptimal, 
			(VkBuffer)buffer->instance, command.regionCount, bufferImageCopies.data());
	}
	else
	{
		commandBuffer.copyBufferToImage((VkBuffer)buffer->instance, (VkImage)image->instance, 
			vk::ImageLayout::eTransferDstOptimal, command.regionCount, bufferImageCopies.data());
	}
}

//**********************************************************************************************************************
static vector<vk::ImageBlit> imageBlits;

void CommandBuffer::processCommand(const BlitImageCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto srcImage = GraphicsAPI::imagePool.get(command.source);
	auto dstImage = GraphicsAPI::imagePool.get(command.destination);
	auto regions = (const Image::BlitRegion*)( (const uint8*)&command + sizeof(BlitImageCommandBase));
	auto srcAspectFlags = toVkImageAspectFlags(srcImage->format);
	auto dstAspectFlags = toVkImageAspectFlags(dstImage->format);
	uint32 oldPipelineStage = 0, newPipelineStage = (uint32)vk::PipelineStageFlagBits::eTransfer;

	if (imageBlits.size() < command.regionCount)
		imageBlits.resize(command.regionCount);

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
			region.layerCount == 0 ? srcImage->layerCount : region.layerCount;
		array<vk::Offset3D, 2> srcBounds;
		srcBounds[0] = vk::Offset3D(region.srcOffset.x, region.srcOffset.y, region.srcOffset.z);
		array<vk::Offset3D, 2> dstBounds;
		dstBounds[0] = vk::Offset3D(region.dstOffset.x, region.dstOffset.y, region.dstOffset.z);

		if (region.srcExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(srcImage->size, region.srcMipLevel);
			srcBounds[1] = vk::Offset3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			srcBounds[1] = vk::Offset3D(region.srcExtent.x, region.srcExtent.y, region.srcExtent.z);
		}
		if (region.dstExtent == 0u)
		{
			auto mipImageSize = calcSizeAtMip(dstImage->size, region.dstMipLevel);
			dstBounds[1] = vk::Offset3D(mipImageSize.x, mipImageSize.y, mipImageSize.z);
		}
		else
		{
			dstBounds[1] = vk::Offset3D(region.dstExtent.x, region.dstExtent.y, region.dstExtent.z);
		}

		imageBlits[i] = vk::ImageBlit(srcSubresource, srcBounds, dstSubresource, dstBounds);

		// TODO: possibly somehow combine these barriers?
		for (uint32 j = 0; j < srcSubresource.layerCount; j++)
		{
			auto& oldSrcImageState = getImageState(command.source, region.srcMipLevel, region.srcBaseLayer + j);
			oldPipelineStage |= oldSrcImageState.stage;

			if (!isSameState(oldSrcImageState, newSrcImageState))
			{
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldSrcImageState.access), vk::AccessFlags(newSrcImageState.access),
					(vk::ImageLayout)oldSrcImageState.layout, (vk::ImageLayout)newSrcImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkImage)srcImage->instance, vk::ImageSubresourceRange(
						srcAspectFlags, region.srcMipLevel, 1, region.srcBaseLayer + j, 1));
				imageMemoryBarriers.push_back(imageMemoryBarrier);
			}
			oldSrcImageState = newSrcImageState;

			auto& oldDstImageState = getImageState(command.destination, region.dstMipLevel, region.dstBaseLayer + j);
			oldPipelineStage |= oldDstImageState.stage;

			if (!isSameState(oldDstImageState, newDstImageState))
			{
				vk::ImageMemoryBarrier imageMemoryBarrier(
					vk::AccessFlags(oldDstImageState.access), vk::AccessFlags(newDstImageState.access),
					(vk::ImageLayout)oldDstImageState.layout, (vk::ImageLayout)newDstImageState.layout,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					(VkImage)dstImage->instance, vk::ImageSubresourceRange(
						dstAspectFlags, region.dstMipLevel, 1, region.dstBaseLayer + j, 1));
				imageMemoryBarriers.push_back(imageMemoryBarrier);
			}
			oldDstImageState = newDstImageState;
		}
	}

	processPipelineBarriers(oldPipelineStage, newPipelineStage);

	commandBuffer.blitImage(
		(VkImage)srcImage->instance, vk::ImageLayout::eTransferSrcOptimal,
		(VkImage)dstImage->instance, vk::ImageLayout::eTransferDstOptimal,
		command.regionCount, imageBlits.data(), (vk::Filter)command.filter);
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void CommandBuffer::processCommand(const BeginLabelCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	auto floatColor = (float4)command.color;
	array<float, 4> values = { floatColor.x, floatColor.y, floatColor.z, floatColor.w };
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	commandBuffer.beginDebugUtilsLabelEXT(debugLabel, Vulkan::dynamicLoader);
}
void CommandBuffer::processCommand(const EndLabelCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	commandBuffer.endDebugUtilsLabelEXT(Vulkan::dynamicLoader);
}
void CommandBuffer::processCommand(const InsertLabelCommand& command)
{
	vk::CommandBuffer commandBuffer((VkCommandBuffer)instance);
	auto name = (const char*)&command + sizeof(BeginLabelCommandBase);
	auto floatColor = (float4)command.color;
	array<float, 4> values = { floatColor.x, floatColor.y, floatColor.z, floatColor.w };
	vk::DebugUtilsLabelEXT debugLabel(name, values);
	commandBuffer.beginDebugUtilsLabelEXT(debugLabel, Vulkan::dynamicLoader);
}
#endif

//**********************************************************************************************************************
void CommandBuffer::addCommand(const BeginRenderPassCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)(sizeof(BeginRenderPassCommandBase) + command.clearColorCount * sizeof(float4));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(BeginRenderPassCommandBase));

	if (command.clearColorCount > 0)
	{
		memcpy((uint8*)allocation + sizeof(BeginRenderPassCommandBase),
			command.clearColors, command.clearColorCount * sizeof(float4));
	}

	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const NextSubpassCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(NextSubpassCommand);
	auto allocation = (NextSubpassCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const ExecuteCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics ||
		type == CommandBufferType::TransferOnly || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(ExecuteCommandBase) + command.bufferCount * sizeof(void*));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(ExecuteCommandBase));
	memcpy((uint8*)allocation + sizeof(ExecuteCommandBase),
		command.buffers, command.bufferCount * sizeof(void*));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const EndRenderPassCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(EndRenderPassCommand);
	auto allocation = allocateCommand(commandSize);
	allocation->type = command.type;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}

//**********************************************************************************************************************
void CommandBuffer::addCommand(const ClearAttachmentsCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto attachmentsSize = command.attachmentCount * sizeof(Framebuffer::ClearAttachment);
	auto commandSize = (uint32)(sizeof(ClearAttachmentsCommandBase) +
		attachmentsSize + command.regionCount * sizeof(Framebuffer::ClearRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(ClearAttachmentsCommandBase));
	memcpy((uint8*)allocation + sizeof(ClearAttachmentsCommandBase),
		command.attachments, command.attachmentCount * sizeof(Framebuffer::ClearAttachment));
	memcpy((uint8*)allocation + sizeof(ClearAttachmentsCommandBase) + attachmentsSize,
		command.regions, command.regionCount * sizeof(Framebuffer::ClearRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const BindPipelineCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame ||
		type == CommandBufferType::Graphics || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)sizeof(BindPipelineCommand);
	auto allocation = (BindPipelineCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const BindDescriptorSetsCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame ||
		type == CommandBufferType::Graphics || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(BindDescriptorSetsCommandBase) + 
		command.rangeCount * sizeof(DescriptorSet::Range));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(BindDescriptorSetsCommandBase));
	memcpy((uint8*)allocation + sizeof(BindDescriptorSetsCommandBase),
		command.descriptorSetRange, command.rangeCount * sizeof(DescriptorSet::Range));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const PushConstantsCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame ||
		type == CommandBufferType::Graphics || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(PushConstantsCommandBase) + alignSize(command.dataSize));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(PushConstantsCommandBase));
	memcpy((uint8*)allocation + sizeof(PushConstantsCommandBase), command.data, command.dataSize);
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}

//**********************************************************************************************************************
void CommandBuffer::addCommand(const SetViewportCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(SetViewportCommand);
	auto allocation = (SetViewportCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const SetScissorCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(SetScissorCommand);
	auto allocation = (SetScissorCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const SetViewportScissorCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(SetViewportScissorCommand);
	auto allocation = (SetViewportScissorCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const DrawCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(DrawCommand);
	auto allocation = (DrawCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const DrawIndexedCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)sizeof(DrawIndexedCommand);
	auto allocation = (DrawIndexedCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const DispatchCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame ||
		type == CommandBufferType::Graphics || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)sizeof(DispatchCommand);
	auto allocation = (DispatchCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}

//**********************************************************************************************************************
void CommandBuffer::addCommand(const FillBufferCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics ||
		type == CommandBufferType::TransferOnly || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)sizeof(FillBufferCommand);
	auto allocation = (FillBufferCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const CopyBufferCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics ||
		type == CommandBufferType::TransferOnly || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(CopyBufferCommandBase) + command.regionCount * sizeof(Buffer::CopyRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(CopyBufferCommandBase));
	memcpy((uint8*)allocation + sizeof(CopyBufferCommandBase),
		command.regions, command.regionCount * sizeof(Buffer::CopyRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}

//**********************************************************************************************************************
void CommandBuffer::addCommand(const ClearImageCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame ||
		type == CommandBufferType::Graphics || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(ClearImageCommandBase) + command.regionCount * sizeof(Image::ClearRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(ClearImageCommandBase));
	memcpy((uint8*)allocation + sizeof(ClearImageCommandBase),
		command.regions, command.regionCount * sizeof(Image::ClearRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const CopyImageCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics ||
		type == CommandBufferType::TransferOnly || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(CopyImageCommandBase) +
		command.regionCount * sizeof(Image::CopyImageRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(CopyImageCommandBase));
	memcpy((uint8*)allocation + sizeof(CopyImageCommandBase),
		command.regions, command.regionCount * sizeof(Image::CopyImageRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const CopyBufferImageCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics ||
		type == CommandBufferType::TransferOnly || type == CommandBufferType::ComputeOnly);
	auto commandSize = (uint32)(sizeof(CopyBufferImageCommandBase) +
		command.regionCount * sizeof(Image::CopyBufferRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(CopyBufferImageCommandBase));
	memcpy((uint8*)allocation + sizeof(CopyBufferImageCommandBase),
		command.regions, command.regionCount * sizeof(Image::CopyBufferRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const BlitImageCommand& command)
{
	GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
	auto commandSize = (uint32)(sizeof(BlitImageCommandBase) + command.regionCount * sizeof(Image::BlitRegion));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(BlitImageCommandBase));
	memcpy((uint8*)allocation + sizeof(BlitImageCommandBase),
		command.regions, command.regionCount * sizeof(Image::BlitRegion));
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void CommandBuffer::addCommand(const BeginLabelCommand& command)
{
	auto nameLength = strlen(command.name) + 1;
	auto commandSize = (uint32)(sizeof(BeginLabelCommandBase) + alignSize(nameLength));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(BeginLabelCommandBase));
	memcpy((uint8*)allocation + sizeof(BeginLabelCommandBase), command.name, nameLength);
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
void CommandBuffer::addCommand(const EndLabelCommand& command)
{
	auto commandSize = (uint32)sizeof(EndLabelCommand);
	auto allocation = (EndLabelCommand*)allocateCommand(commandSize);
	*allocation = command;
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
}
void CommandBuffer::addCommand(const InsertLabelCommand& command)
{
	auto nameLength = strlen(command.name) + 1;
	auto commandSize = (uint32)(sizeof(InsertLabelCommandBase) + alignSize(nameLength));
	auto allocation = allocateCommand(commandSize);
	memcpy((uint8*)allocation, &command, sizeof(InsertLabelCommandBase));
	memcpy((uint8*)allocation + sizeof(InsertLabelCommandBase), command.name, nameLength);
	allocation->thisSize = commandSize;
	allocation->lastSize = lastSize;
	lastSize = commandSize;
	hasAnyCommand = true;
}
#endif