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

/***********************************************************************************************************************
 * @file
 * @brief Graphics command buffer functions.
 */

#pragma once
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/pipeline/ray-tracing.hpp"
#include "garden/graphics/acceleration-structure/tlas.hpp"
#include "garden/thread-pool.hpp"

namespace garden::graphics
{

struct Command
{
	enum class Type : uint8
	{
		Unknown, BufferBarrier, BeginRenderPass, NextSubpass, Execute, EndRenderPass, ClearAttachments,
		BindPipeline, BindDescriptorSets, PushConstants, SetViewport, SetScissor,
		SetViewportScissor, SetDepthBias, Draw, DrawIndexed, DrawIndirect, DrawIndexedIndirect, 
		Dispatch, FillBuffer, CopyBuffer, ClearImage, CopyImage, CopyBufferImage, BlitImage,
		BuildAccelerationStructure, CopyAccelerationStructure, TraceRays, Custom,

		#if GARDEN_DEBUG
		BeginLabel, EndLabel, InsertLabel,
		#endif

		Count
	};

	uint32 thisSize = 0;
	uint32 lastSize = 0;
	Type type = {};

	Command(Type type) noexcept : type(type) { }
};

struct BufferBarrierCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 bufferCount = 0;
	Buffer::BarrierState newState = {}; // TODO: state for each buffer?
	BufferBarrierCommandBase() noexcept : Command(Type::BufferBarrier) { }
};
struct BufferBarrierCommand final : public BufferBarrierCommandBase
{
	const ID<Buffer>* buffers = nullptr;
};

//**********************************************************************************************************************
struct BeginRenderPassCommandBase : public Command
{
	uint8 asyncRecording = false;
	uint8 clearColorCount = 0;
	uint8 _alignment = 0;
	ID<Framebuffer> framebuffer = {};
	float clearDepth = 0.0f;
	uint32 clearStencil = 0x00;
	int4 region = int4::zero;
	BeginRenderPassCommandBase() noexcept : Command(Type::BeginRenderPass) { }
};
struct BeginRenderPassCommand final : public BeginRenderPassCommandBase
{
	const float4* clearColors = nullptr; // Do not use f32x4, unaligned memory!
};
struct NextSubpassCommand final : public Command
{
	uint8 asyncRecording = false;
	uint16 _alignment = 0;
	NextSubpassCommand() noexcept : Command(Type::NextSubpass) { }
};
struct ExecuteCommandBase : public Command
{
	uint8 _alignment = 0;
	uint16 bufferCount = 0;
	uint32 asyncCommandCount = 0;
	ExecuteCommandBase() noexcept : Command(Type::Execute) { }
};
struct ExecuteCommand final : public ExecuteCommandBase
{
	void* buffers = nullptr;
};
struct EndRenderPassCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	EndRenderPassCommand() noexcept : Command(Type::EndRenderPass) { }
};

//**********************************************************************************************************************
struct ClearAttachmentsCommandBase : public Command
{
	uint8 attachmentCount = 0;
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Framebuffer> framebuffer = {};
	ClearAttachmentsCommandBase() noexcept : Command(Type::ClearAttachments) { }
};
struct ClearAttachmentsCommand final : public ClearAttachmentsCommandBase
{
	const Framebuffer::ClearAttachment* attachments = nullptr;
	const Framebuffer::ClearRegion* regions = nullptr;
};
struct BindPipelineCommand final : public Command
{
	PipelineType pipelineType = {};
	uint8 variant = 0;
	uint8 _alignment = 0;
	ID<Pipeline> pipeline = {};
	BindPipelineCommand() noexcept : Command(Type::BindPipeline) { }
};
struct BindDescriptorSetsCommandBase : public Command
{
	uint8 rangeCount = 0;
	uint16 _alignment = 0;
	BindDescriptorSetsCommandBase() noexcept : Command(Type::BindDescriptorSets) { }
};
struct BindDescriptorSetsCommand final : public BindDescriptorSetsCommandBase
{
	const DescriptorSet::Range* descriptorSetRanges = nullptr;
};
struct BindDescriptorSetsAsyncCommand final : public BindDescriptorSetsCommandBase
{
	DescriptorSet::Range descriptorSetRanges[3]; // TODO: Looks like there is no more than 3 for an async bind. Rethink later?
};

//**********************************************************************************************************************
struct PushConstantsCommandBase : public Command
{
	uint8 _alignment = 0;
	uint16 dataSize = 0;
	uint32 pipelineStages = 0;
	void* pipelineLayout = nullptr;
	PushConstantsCommandBase() noexcept : Command(Type::PushConstants) { }
};
struct PushConstantsCommand final : public PushConstantsCommandBase
{
	const void* data = nullptr;
};
struct SetViewportCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewport = float4::zero;
	int2 framebufferSize = int2::zero;
	SetViewportCommand() noexcept : Command(Type::SetViewport) { }
};
struct SetScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	int4 scissor = int4::zero;
	int2 framebufferSize = int2::zero;
	SetScissorCommand() noexcept : Command(Type::SetScissor) { }
};
struct SetViewportScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewportScissor = float4::zero;
	int2 framebufferSize = int2::zero;
	SetViewportScissorCommand() noexcept : Command(Type::SetViewportScissor) { }
};
struct SetDepthBiasCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float constantFactor = 0.0f;
	float slopeFactor = 0.0f;
	float clamp = 0.0f;
	SetDepthBiasCommand() noexcept : Command(Type::SetDepthBias) { }
};

//**********************************************************************************************************************
struct DrawCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 vertexCount = 0;
	uint32 instanceCount = 0;
	uint32 vertexOffset = 0;
	uint32 instanceOffset = 0;
	ID<Buffer> vertexBuffer = {};
	DrawCommand() noexcept : Command(Type::Draw) { }
};
struct DrawIndexedCommand final : public Command
{
	IndexType indexType = {};
	uint16 _alignment = 0;
	uint32 indexCount = 0;
	uint32 instanceCount = 0;
	uint32 indexOffset = 0;
	uint32 vertexOffset = 0;
	uint32 instanceOffset = 0;
	ID<Buffer> vertexBuffer = {};
	ID<Buffer> indexBuffer = {};
	DrawIndexedCommand() noexcept : Command(Type::DrawIndexed) { }
};
struct DrawIndirectCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 offset = 0;
	uint32 drawCount = 0;
	uint32 stride = 0;
	ID<Buffer> buffer = {};
	DrawIndirectCommand() noexcept : Command(Type::DrawIndirect) { }
};
struct DrawIndexedIndirectCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 offset = 0;
	uint32 drawCount = 0;
	uint32 stride = 0;
	ID<Buffer> buffer = {};
	DrawIndexedIndirectCommand() noexcept : Command(Type::DrawIndexedIndirect) { }
};

struct DispatchCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint3 groupCount = uint3::zero;
	DispatchCommand() noexcept : Command(Type::Dispatch) { }
};

//**********************************************************************************************************************
struct FillBufferCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	ID<Buffer> buffer = {};
	uint32 data = 0;
	uint64 size = 0;
	uint64 offset = 0;
	FillBufferCommand() noexcept : Command(Type::FillBuffer) { }
};
struct CopyBufferCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 regionCount = 0;
	ID<Buffer> source = {};
	ID<Buffer> destination = {};
	CopyBufferCommandBase() noexcept : Command(Type::CopyBuffer) { }
};
struct CopyBufferCommand final : public CopyBufferCommandBase
{
	const Buffer::CopyRegion* regions = nullptr;
};

//**********************************************************************************************************************
struct ClearImageCommandBase : public Command
{
	uint8 clearType = 0;
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Image> image = {};
	float4 color = float4::zero;
	ClearImageCommandBase() noexcept : Command(Type::ClearImage) { }
};
struct ClearImageCommand final : public ClearImageCommandBase
{
	const Image::ClearRegion* regions = nullptr;
};
struct CopyImageCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 regionCount = 0;
	ID<Image> source = {};
	ID<Image> destination = {};
	CopyImageCommandBase() noexcept : Command(Type::CopyImage) { }
};
struct CopyImageCommand final : public CopyImageCommandBase
{
	const Image::CopyImageRegion* regions = nullptr;
};
struct CopyBufferImageCommandBase : public Command
{
	uint8 toBuffer = false;
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Buffer> buffer = {};
	ID<Image> image = {};
	CopyBufferImageCommandBase() noexcept : Command(Type::CopyBufferImage) { }
};
struct CopyBufferImageCommand final : public CopyBufferImageCommandBase
{
	const Image::CopyBufferRegion* regions = nullptr;
};
struct BlitImageCommandBase : public Command
{
	Sampler::Filter filter = {};
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Image> source = {};
	ID<Image> destination = {};
	BlitImageCommandBase() noexcept : Command(Type::BlitImage) { }
};
struct BlitImageCommand final : public BlitImageCommandBase
{
	const Image::BlitRegion* regions = nullptr;
};

//**********************************************************************************************************************
struct BuildAccelerationStructureCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint8 isUpdate = 0;
	AccelerationStructure::Type typeAS = {};
	ID<AccelerationStructure> srcAS = {};
	ID<AccelerationStructure> dstAS = {};
	ID<Buffer> scratchBuffer = {};
	BuildAccelerationStructureCommand() noexcept : Command(Type::BuildAccelerationStructure) { }
};
struct CopyAccelerationStructureCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint8 isCompact = 0;
	AccelerationStructure::Type typeAS = {};
	ID<AccelerationStructure> srcAS = {};
	ID<AccelerationStructure> dstAS = {};
	CopyAccelerationStructureCommand() noexcept : Command(Type::CopyAccelerationStructure) { }
};
struct TraceRaysCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint3 groupCount = uint3::zero;
	RayTracingPipeline::SbtGroupRegions sbtRegions = {};
	ID<Buffer> sbtBuffer = {};
	TraceRaysCommand() noexcept : Command(Type::TraceRays) { }
};

struct CustomRenderCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	void(*onCommand)(void*, void*) = nullptr;
	void* argument = nullptr;
	CustomRenderCommand() noexcept : Command(Type::Custom) { }
};
union AsyncRenderCommand final
{
	Command base;
	BindPipelineCommand bindPipeline;
	BindDescriptorSetsAsyncCommand bindDescriptorSets;
	DrawCommand draw;
	DrawIndexedCommand drawIndexed;

	AsyncRenderCommand(const BindPipelineCommand& command) noexcept : bindPipeline(command) { }
	AsyncRenderCommand(const BindDescriptorSetsAsyncCommand& command) noexcept : bindDescriptorSets(command) { }
	AsyncRenderCommand(const DrawCommand& command) noexcept : draw(command) { }
	AsyncRenderCommand(const DrawIndexedCommand& command) noexcept : drawIndexed(command) { }
};

#if GARDEN_DEBUG
//**********************************************************************************************************************
struct BeginLabelCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	Color color = Color::transparent;
	BeginLabelCommandBase() noexcept : Command(Type::BeginLabel) { }
};
struct BeginLabelCommand final : public BeginLabelCommandBase
{
	const char* name = nullptr;
};
struct EndLabelCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	EndLabelCommand() noexcept : Command(Type::EndLabel) { }
};
struct InsertLabelCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	Color color = Color::transparent;
	InsertLabelCommandBase() noexcept : Command(Type::InsertLabel) { }
};
struct InsertLabelCommand final : public InsertLabelCommandBase
{
	const char* name = nullptr;
};
#endif

/***********************************************************************************************************************
 * @brief Base rendering commands recorder.
 * 
 * @details
 * Command buffer is a fundamental object used to record commands that can be submitted to the GPU for execution. 
 * These commands can include drawing operations, compute dispatches, memory transfers, setting up rendering states, 
 * and more. Command buffers are a key part of engine design to allow for explicit, low-level control over the GPU, 
 * providing both flexibility and the potential for significant performance optimizations.
 */
class CommandBuffer
{
public:
	typedef tsl::robin_map<uint64, uint32> LockResources;
	static constexpr psize dataAlignment = 4; /**< Command buffer data alignment. */

	static constexpr auto asyncCommandOffset = sizeof(uint32) * 2;
	static constexpr auto asyncCommandSize = sizeof(AsyncRenderCommand) - asyncCommandOffset;
	// Note: skipping command size part, because size is fixed for async.

	// Note: optimal for little endian arch.
	struct ResourceKey { ID<Resource> resource; ResourceType type; }; 

	struct AsyncData
	{
	 	LockResources lockingResources;
		uint8* data = nullptr;
		psize size = 0, capacity = 16;
	};
protected:
	LockResources lockedResources;
	LockResources lockingResources;
	vector<AsyncData> asyncData;
	ThreadPool* threadPool = nullptr;
	uint8* data = nullptr;
	psize size = 0, capacity = 16;
	uint8* dataIter = nullptr, *dataEnd = nullptr;
	uint32 lastSize = 0;
	CommandBufferType type = {};
	bool noSubpass = false;
	bool isRunning = false;
	volatile bool hasAnyCommand = false;

	template<class T = Command>
	T* allocateCommand(const T& command, psize size, int32 threadIndex = -1)
	{
		T* allocation;
		if (threadIndex < 0)
		{
			if (this->size + size > this->capacity)
			{
				this->capacity = this->size + size;
				this->data = realloc<uint8>(this->data, this->capacity);
			}

			allocation = (T*)(this->data + this->size);
			*allocation = command; allocation->lastSize = lastSize;
			this->size += size; this->lastSize = size;
		}
		else
		{
			GARDEN_ASSERT(threadPool);
			GARDEN_ASSERT(threadIndex < threadPool->getThreadCount());

			auto& async = asyncData[threadIndex];
			if (async.size + size > async.capacity)
			{
				async.capacity = async.size + size;
				async.data = realloc<uint8>(async.data, capacity);
			}

			allocation = (T*)(async.data + async.size);
			*allocation = command; async.size += size;
		}

		allocation->thisSize = size;
		return allocation;
	}
	template<class T = Command>
	T* allocateCommand(const T& command) { return allocateCommand(command, sizeof(T)); }

	void processCommands();

	virtual void processCommand(const BufferBarrierCommand& command) = 0;
	virtual void processCommand(const BeginRenderPassCommand& command) = 0;
	virtual void processCommand(const NextSubpassCommand& command) = 0;
	virtual void processCommand(const ExecuteCommand& command) = 0;
	virtual void processCommand(const EndRenderPassCommand& command) = 0;
	virtual void processCommand(const ClearAttachmentsCommand& command) = 0;
	virtual void processCommand(const BindPipelineCommand& command) = 0;
	virtual void processCommand(const BindDescriptorSetsCommand& command) = 0;
	virtual void processCommand(const PushConstantsCommand& command) = 0;
	virtual void processCommand(const SetViewportCommand& command) = 0;
	virtual void processCommand(const SetScissorCommand& command) = 0;
	virtual void processCommand(const SetViewportScissorCommand& command) = 0;
	virtual void processCommand(const SetDepthBiasCommand& command) = 0;
	virtual void processCommand(const DrawCommand& command) = 0;
	virtual void processCommand(const DrawIndexedCommand& command) = 0;
	virtual void processCommand(const DispatchCommand& command) = 0;
	virtual void processCommand(const FillBufferCommand& command) = 0;
	virtual void processCommand(const CopyBufferCommand& command) = 0;
	virtual void processCommand(const ClearImageCommand& command) = 0;
	virtual void processCommand(const CopyImageCommand& command) = 0;
	virtual void processCommand(const CopyBufferImageCommand& command) = 0;
	virtual void processCommand(const BlitImageCommand& command) = 0;
	virtual void processCommand(const BuildAccelerationStructureCommand& command) = 0;
	virtual void processCommand(const CopyAccelerationStructureCommand& command) = 0;
	virtual void processCommand(const TraceRaysCommand& command) = 0;
	virtual void processCommand(const CustomRenderCommand& command) = 0;

	#if GARDEN_DEBUG
	virtual void processCommand(const BeginLabelCommand& command) = 0;
	virtual void processCommand(const EndLabelCommand& command) = 0;
	virtual void processCommand(const InsertLabelCommand& command) = 0;
	#endif

	void flushLockedResources(LockResources& lockedResources);
public:
	/*******************************************************************************************************************
	 * @brief Creates a new command buffer instance.
	 *
	 * @param[in] threadPool thread pool instance or null
	 * @param type target command buffer type
	 */
	CommandBuffer(ThreadPool* threadPool, CommandBufferType type);
	/**
	 * @brief Destroys command buffer instance.
	 */
	virtual ~CommandBuffer();

	/**
	 * @brief Return command buffer type.
	 */
	CommandBufferType getType() const noexcept { return type; }

	void addCommand(const BufferBarrierCommand& command)
	{
		auto commandSize = sizeof(BufferBarrierCommandBase) + command.bufferCount * sizeof(ID<Buffer>);
		auto allocation = allocateCommand<BufferBarrierCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.buffers, command.bufferCount * sizeof(ID<Buffer>));
	}
	void addCommand(const BeginRenderPassCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		auto commandSize = sizeof(BeginRenderPassCommandBase) + command.clearColorCount * sizeof(float4);
		auto allocation = allocateCommand<BeginRenderPassCommandBase>(command, commandSize);
		if (command.clearColorCount > 0)
			memcpy(allocation + 1, command.clearColors, command.clearColorCount * sizeof(float4));
		hasAnyCommand = true;
	}
	void addCommand(const NextSubpassCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const ExecuteCommand& command)
	{
		psize asynSize = 0;
		for (const auto& async : asyncData)
			asynSize += async.size;
		if (asynSize == 0) return;

		GARDEN_ASSERT(asynSize % asyncCommandSize == 0);
		auto bufferBinarySize = command.bufferCount * sizeof(void*);
		auto commandSize = sizeof(ExecuteCommandBase) + bufferBinarySize + asynSize;
		auto allocation = allocateCommand<ExecuteCommandBase>(command, commandSize);
		allocation->asyncCommandCount = asynSize / asyncCommandSize;

		auto data = (uint8*)allocation + sizeof(ExecuteCommandBase);
		memcpy(data, command.buffers, bufferBinarySize);
		data += bufferBinarySize;

		for (auto& async : asyncData)
		{
			if (async.size == 0)
				continue;
			memcpy(data, async.data, async.size); data += async.size;

			for (auto pair : async.lockingResources)
			{
				auto result = lockingResources.find(pair.first);
				if (result == lockingResources.end())
					lockingResources.emplace(pair.first, pair.second);
				else result.value() += pair.second;
			}
			async.lockingResources.clear(); async.size = 0;
		}
		GARDEN_ASSERT(data == (uint8*)allocation + commandSize);
	}
	void addCommand(const EndRenderPassCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}

	//******************************************************************************************************************
	void addCommand(const ClearAttachmentsCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		auto attachmentsSize = command.attachmentCount * sizeof(Framebuffer::ClearAttachment);
		auto commandSize = sizeof(ClearAttachmentsCommandBase) +
			attachmentsSize + command.regionCount * sizeof(Framebuffer::ClearRegion);
		auto allocation = allocateCommand<ClearAttachmentsCommandBase>(command, commandSize);
		memcpy((uint8*)allocation + sizeof(ClearAttachmentsCommandBase),
			command.attachments, command.attachmentCount * sizeof(Framebuffer::ClearAttachment));
		memcpy((uint8*)allocation + sizeof(ClearAttachmentsCommandBase) + attachmentsSize,
			command.regions, command.regionCount * sizeof(Framebuffer::ClearRegion));
	}
	void addCommand(const BindPipelineCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const BindDescriptorSetsCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		auto commandSize = sizeof(BindDescriptorSetsCommandBase) + command.rangeCount * sizeof(DescriptorSet::Range);
		auto allocation = allocateCommand<BindDescriptorSetsCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.descriptorSetRanges, command.rangeCount * sizeof(DescriptorSet::Range));
	}
	void addCommand(const BindDescriptorSetsAsyncCommand& command, int32 threadIndex = -1)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		allocateCommand(command, sizeof(BindDescriptorSetsAsyncCommand), threadIndex);
	}
	void addCommand(const PushConstantsCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		auto commandSize = sizeof(PushConstantsCommandBase) + alignSize((psize)command.dataSize, dataAlignment);
		auto allocation = allocateCommand<PushConstantsCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.data, command.dataSize);
	}

	//******************************************************************************************************************
	void addCommand(const SetViewportCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const SetScissorCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const SetViewportScissorCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const SetDepthBiasCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const DrawCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const DrawIndexedCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		allocateCommand(command);
	}
	void addCommand(const DispatchCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		allocateCommand(command); hasAnyCommand = true;
	}

	//******************************************************************************************************************
	void addCommand(const FillBufferCommand& command)
	{
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const CopyBufferCommand& command)
	{
		auto commandSize = sizeof(CopyBufferCommandBase) + command.regionCount * sizeof(Buffer::CopyRegion);
		auto allocation = allocateCommand<CopyBufferCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.regions, command.regionCount * sizeof(Buffer::CopyRegion));
		hasAnyCommand = true;
	}
	void addCommand(const ClearImageCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame ||
			type == CommandBufferType::Graphics || type == CommandBufferType::Compute);
		auto commandSize = sizeof(ClearImageCommandBase) + command.regionCount * sizeof(Image::ClearRegion);
		auto allocation = allocateCommand<ClearImageCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.regions, command.regionCount * sizeof(Image::ClearRegion));
		hasAnyCommand = true;
	}
	void addCommand(const CopyImageCommand& command)
	{
		auto commandSize = sizeof(CopyImageCommandBase) + command.regionCount * sizeof(Image::CopyImageRegion);
		auto allocation = allocateCommand<CopyImageCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.regions, command.regionCount * sizeof(Image::CopyImageRegion));
		hasAnyCommand = true;
	}
	void addCommand(const CopyBufferImageCommand& command)
	{
		auto commandSize = sizeof(CopyBufferImageCommandBase) + command.regionCount * sizeof(Image::CopyBufferRegion);
		auto allocation = allocateCommand<CopyBufferImageCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.regions, command.regionCount * sizeof(Image::CopyBufferRegion));
		hasAnyCommand = true;
	}
	void addCommand(const BlitImageCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);
		auto commandSize = sizeof(BlitImageCommandBase) + command.regionCount * sizeof(Image::BlitRegion);
		auto allocation = allocateCommand<BlitImageCommandBase>(command, commandSize);
		memcpy(allocation + 1, command.regions, command.regionCount * sizeof(Image::BlitRegion));
		hasAnyCommand = true;
	}

	//******************************************************************************************************************
	void addCommand(const BuildAccelerationStructureCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Compute);
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const CopyAccelerationStructureCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Compute);
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const TraceRaysCommand& command)
	{
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Compute);
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const CustomRenderCommand& command)
	{
		allocateCommand(command); hasAnyCommand = true;
	}
	void addCommand(const AsyncRenderCommand& command, int32 threadIndex)
	{
		GARDEN_ASSERT(threadPool);
		GARDEN_ASSERT(threadIndex < threadPool->getThreadCount());
		GARDEN_ASSERT(type == CommandBufferType::Frame || type == CommandBufferType::Graphics);

		auto& async = asyncData[threadIndex];		
		if (async.size + asyncCommandSize > async.capacity)
		{
			async.capacity = async.size + sizeof(AsyncRenderCommand);
			async.data = realloc<uint8>(async.data, capacity);
		}

		memcpy(async.data + async.size, (const uint8*)&command + asyncCommandOffset, asyncCommandSize);
		async.size += asyncCommandSize;
	}

	#if GARDEN_DEBUG
	void addCommand(const BeginLabelCommand& command, int32 threadIndex)
	{
		auto nameLength = strlen(command.name) + 1;
		auto commandSize = sizeof(BeginLabelCommandBase) + alignSize(nameLength, dataAlignment);
		auto allocation = allocateCommand<BeginLabelCommandBase>(command, commandSize, threadIndex);
		memcpy(allocation + 1, command.name, nameLength);
	}
	void addCommand(const EndLabelCommand& command, int32 threadIndex)
	{
		allocateCommand(command, sizeof(EndLabelCommand), threadIndex);
	}
	void addCommand(const InsertLabelCommand& command, int32 threadIndex)
	{
		auto nameLength = strlen(command.name) + 1;
		auto commandSize = sizeof(InsertLabelCommandBase) + alignSize(nameLength, dataAlignment);
		auto allocation = allocateCommand<InsertLabelCommandBase>(command, commandSize, threadIndex);
		memcpy(allocation + 1, command.name, nameLength);
	}
	#endif

	/*******************************************************************************************************************
	 * @brief Submits recorded command to the GPU.
	 */
	virtual void submit() = 0;
	/**
	 * @brief Returns true if command buffer is busy right now.
	 */
	virtual bool isBusy() = 0;

	void addLockedResource(ResourceType type, ID<Resource> resource, int32 threadIndex)
	{
		LockResources* lockResources;
		if (threadIndex >= 0)
		{
			GARDEN_ASSERT(threadPool);
			GARDEN_ASSERT(threadIndex < threadPool->getThreadCount());
			lockResources = &asyncData[threadIndex].lockingResources;
		}
		else lockResources = &lockingResources;

		ResourceKey key = { resource, type };
		auto hash = *((const uint64*)&key);
		auto result = lockResources->find(hash);
		if (result == lockResources->end())
			lockResources->emplace(hash, 1);
		else result.value()++;
	}

	void addLockedResource(ID<Buffer> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Buffer, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<Image> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Image, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<ImageView> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::ImageView, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<Framebuffer> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Framebuffer, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<Sampler> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Sampler, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<Blas> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Blas, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<Tlas> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::Tlas, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<GraphicsPipeline> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::GraphicsPipeline, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<ComputePipeline> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::ComputePipeline, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<RayTracingPipeline> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::RayTracingPipeline, ID<Resource>(resource), threadIndex); }
	void addLockedResource(ID<DescriptorSet> resource, int32 threadIndex = -1)
	{ addLockedResource(ResourceType::DescriptorSet, ID<Resource>(resource), threadIndex); }
};

} // namespace garden::graphics