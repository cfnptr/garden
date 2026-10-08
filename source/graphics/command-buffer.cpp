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

#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/api.hpp"
#include "garden/profiler.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
CommandBuffer::CommandBuffer(ThreadPool* threadPool, CommandBufferType type) : threadPool(threadPool)
{
	this->type = type;
	data = malloc<uint8>(capacity);
	
	if (threadPool)
	{
		asyncData.resize(threadPool->getThreadCount());
		for (auto& async : asyncData)
			async.data = malloc<uint8>(capacity);
	}
}
CommandBuffer::~CommandBuffer()
{
	if (type != CommandBufferType::Frame)
	{
		flushLockedResources(lockedResources);
		flushLockedResources(lockingResources);
	}

	for (const auto& async : asyncData)
		free(async.data);
	free(data);
}

//**********************************************************************************************************************
void CommandBuffer::processCommands()
{
	SET_CPU_ZONE_SCOPED("Command Buffer Process");

	dataIter = data, dataEnd = data + size;
	while (dataIter < dataEnd)
	{
		auto command = (const Command*)dataIter;
		switch (command->type)
		{
		case Command::Type::BufferBarrier:
			processCommand(*(const BufferBarrierCommand*)command); break;
		case Command::Type::BeginRenderPass:
			processCommand(*(const BeginRenderPassCommand*)command); break;
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
		case Command::Type::SetDepthBias:
			processCommand(*(const SetDepthBiasCommand*)command); break;
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
		case Command::Type::BuildAccelerationStructure:
			processCommand(*(const BuildAccelerationStructureCommand*)command); break;
		case Command::Type::CopyAccelerationStructure:
			processCommand(*(const CopyAccelerationStructureCommand*)command); break;
		case Command::Type::TraceRays:
			processCommand(*(const TraceRaysCommand*)command); break;
		case Command::Type::Custom:
			processCommand(*(const CustomRenderCommand*)command); break;

		#if GARDEN_DEBUG
		case Command::Type::BeginLabel:
			processCommand(*(const BeginLabelCommand*)command); break;
		case Command::Type::EndLabel:
			processCommand(*(const EndLabelCommand*)command); break;
		case Command::Type::InsertLabel:
			processCommand(*(const InsertLabelCommand*)command); break;
		#endif

		default: throw GardenError("Not implemented Vulkan command");
		}
		dataIter += command->thisSize;
	}
	GARDEN_ASSERT(dataIter == dataEnd);
}

//**********************************************************************************************************************
void CommandBuffer::lockResource(ResourceType type, ID<Resource> resource, int32 threadIndex)
{
	LockedResources* lockResources;
	if (threadIndex >= 0)
	{
		GARDEN_ASSERT(threadPool);
		GARDEN_ASSERT(threadIndex < threadPool->getThreadCount());
		lockResources = &asyncData[threadIndex].lockingResources;
	}
	else lockResources = &lockingResources;

	ResourceKey key(resource, type);
	if (lockResources->find(key) != lockResources->end())
		return;
	lockResources->emplace(key);

	auto graphicsAPI = GraphicsAPI::get();
	switch (key.type)
	{
	case ResourceType::Buffer:
		graphicsAPI->bufferPool.get(ID<Buffer>(key.resource))->lock(); break;
	case ResourceType::Image:
		graphicsAPI->imagePool.get(ID<Image>(key.resource))->lock(); break;
	case ResourceType::ImageView:
		graphicsAPI->imageViewPool.get(ID<ImageView>(key.resource))->lock(); break;
	case ResourceType::Sampler:
		graphicsAPI->samplerPool.get(ID<Sampler>(key.resource))->lock(); break;
	case ResourceType::Blas:
		graphicsAPI->blasPool.get(ID<Blas>(key.resource))->lock(); break;
	case ResourceType::Tlas:
		graphicsAPI->tlasPool.get(ID<Tlas>(key.resource))->lock(); break;
	case ResourceType::GraphicsPipeline:
		graphicsAPI->graphicsPipelinePool.get(ID<GraphicsPipeline>(key.resource))->lock(); break;
	case ResourceType::ComputePipeline:
		graphicsAPI->computePipelinePool.get(ID<ComputePipeline>(key.resource))->lock(); break;
	case ResourceType::RayTracingPipeline:
		graphicsAPI->rayTracingPipelinePool.get(ID<RayTracingPipeline>(key.resource))->lock(); break;
	case ResourceType::DescriptorSet:
		graphicsAPI->descriptorSetPool.get(ID<DescriptorSet>(key.resource))->lock(); break;
	default: abort();
	}
}

//**********************************************************************************************************************
void CommandBuffer::flushLockedResources(LockedResources& lockedResources)
{
	SET_CPU_ZONE_SCOPED("Locked Resources Flush");

	auto graphicsAPI = GraphicsAPI::get();
	for (auto key : lockedResources)
	{
		switch (key.type)
		{
		case ResourceType::Buffer:
			graphicsAPI->bufferPool.get(ID<Buffer>(key.resource))->unlock(); break;
		case ResourceType::Image:
			graphicsAPI->imagePool.get(ID<Image>(key.resource))->unlock(); break;
		case ResourceType::ImageView:
			graphicsAPI->imageViewPool.get(ID<ImageView>(key.resource))->unlock(); break;
		case ResourceType::Sampler:
			graphicsAPI->samplerPool.get(ID<Sampler>(key.resource))->unlock(); break;
		case ResourceType::Blas:
			graphicsAPI->blasPool.get(ID<Blas>(key.resource))->unlock(); break;
		case ResourceType::Tlas:
			graphicsAPI->tlasPool.get(ID<Tlas>(key.resource))->unlock(); break;
		case ResourceType::GraphicsPipeline:
			graphicsAPI->graphicsPipelinePool.get(ID<GraphicsPipeline>(key.resource))->unlock(); break;
		case ResourceType::ComputePipeline:
			graphicsAPI->computePipelinePool.get(ID<ComputePipeline>(key.resource))->unlock(); break;
		case ResourceType::RayTracingPipeline:
			graphicsAPI->rayTracingPipelinePool.get(ID<RayTracingPipeline>(key.resource))->unlock(); break;
		case ResourceType::DescriptorSet:
			graphicsAPI->descriptorSetPool.get(ID<DescriptorSet>(key.resource))->unlock(); break;
		default: abort();
		}
	}
	lockedResources.clear();
}