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
#include "garden/graphics/api.hpp"
#include "garden/profiler.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
CommandBuffer::CommandBuffer(CommandBufferType type)
{
	data = malloc<uint8>(capacity);
	this->type = type;
}
CommandBuffer::~CommandBuffer()
{
	if (type != CommandBufferType::Frame)
	{
		flushLockedResources(lockedResources);
		flushLockedResources(lockingResources);
	}
	free(data);
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

void CommandBuffer::processCommands()
{
	SET_CPU_ZONE_SCOPED("Command Buffer Process");

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
}

void CommandBuffer::updateImageStates()
{
	auto graphicsAPI = GraphicsAPI::get();
	for (auto pair : imageStates)
	{
		auto imageState = pair.first;
		auto image = graphicsAPI->imagePool.get(imageState.image);
		auto& layouts = ImageExt::getLayouts(**image);
		layouts[imageState.mip * image->getLayerCount() + imageState.layer] = pair.second.layout;
	}
	imageStates.clear();
}

//**********************************************************************************************************************
void CommandBuffer::flushLockedResources(vector<CommandBuffer::LockResource>& lockedResources)
{
	auto graphicsAPI = GraphicsAPI::get();
	for (const auto& pair : lockedResources)
	{
		switch (pair.second)
		{
		case ResourceType::Buffer:
			ResourceExt::getReadyLock(**graphicsAPI->bufferPool.get(ID<Buffer>(pair.first)))--;
			break;
		case ResourceType::Image:
			ResourceExt::getReadyLock(**graphicsAPI->imagePool.get(ID<Image>(pair.first)))--;
			break;
		case ResourceType::ImageView:
			ResourceExt::getReadyLock(**graphicsAPI->imageViewPool.get(ID<ImageView>(pair.first)))--;
			break;
		case ResourceType::Framebuffer:
			ResourceExt::getReadyLock(**graphicsAPI->framebufferPool.get(ID<Framebuffer>(pair.first)))--;
			break;
		case ResourceType::GraphicsPipeline:
			ResourceExt::getReadyLock(**graphicsAPI->graphicsPipelinePool.get(ID<GraphicsPipeline>(pair.first)))--;
			break;
		case ResourceType::ComputePipeline:
			ResourceExt::getReadyLock(**graphicsAPI->computePipelinePool.get(ID<ComputePipeline>(pair.first)))--;
			break;
		case ResourceType::DescriptorSet:
			ResourceExt::getReadyLock(**graphicsAPI->descriptorSetPool.get(ID<DescriptorSet>(pair.first)))--;
			break;
		default: abort();
		}
	}
	lockedResources.clear();
}

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