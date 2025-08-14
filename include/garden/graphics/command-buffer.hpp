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

/***********************************************************************************************************************
 * @file
 * @brief Graphics command buffer functions.
 */

#pragma once
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/pipeline/ray-tracing.hpp"
#include "garden/graphics/acceleration-structure/tlas.hpp"

#include <mutex>

namespace garden::graphics
{

struct Command
{
	enum class Type : uint8
	{
		Unknown, BufferBarrier, BeginRenderPass, NextSubpass, Execute, EndRenderPass, ClearAttachments,
		BindPipeline, BindDescriptorSets, PushConstants, SetViewport, SetScissor,
		SetViewportScissor, Draw, DrawIndexed, Dispatch, // TODO: indirect
		FillBuffer, CopyBuffer, ClearImage, CopyImage, CopyBufferImage, BlitImage,
		SetDepthBias, // TODO: other dynamic setters
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
	uint8 clearColorCount = 0;
	uint8 asyncRecording = false;
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
	uint8 asyncRecording = false;
	uint8 rangeCount = 0;
	uint8 _alignment = 0;
	BindDescriptorSetsCommandBase() noexcept : Command(Type::BindDescriptorSets) { }
};
struct BindDescriptorSetsCommand final : public BindDescriptorSetsCommandBase
{
	const DescriptorSet::Range* descriptorSetRange = nullptr;
};

//**********************************************************************************************************************
struct PushConstantsCommandBase : public Command
{
	uint8 _alignment = 0;
	uint16 dataSize = 0;
	uint32 shaderStages = 0;
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
	SetViewportCommand() noexcept : Command(Type::SetViewport) { }
};
struct SetScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	int4 scissor = int4::zero;
	SetScissorCommand() noexcept : Command(Type::SetScissor) { }
};
struct SetViewportScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewportScissor = float4::zero;
	SetViewportScissorCommand() noexcept : Command(Type::SetViewportScissor) { }
};

//**********************************************************************************************************************
struct DrawCommand final : public Command
{
	uint8 asyncRecording = false;
	uint16 _alignment = 0;
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
	uint8 asyncRecording = false;
	uint8 _alignment = 0;
	uint32 indexCount = 0;
	uint32 instanceCount = 0;
	uint32 indexOffset = 0;
	uint32 vertexOffset = 0;
	uint32 instanceOffset = 0;
	ID<Buffer> vertexBuffer = {};
	ID<Buffer> indexBuffer = {};
	DrawIndexedCommand() noexcept : Command(Type::DrawIndexed) { }
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

#if GARDEN_DEBUG
//**********************************************************************************************************************
struct BeginLabelCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	Color color = Color((uint8)0);
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
	Color color = Color((uint8)0);
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
	typedef pair<ID<Resource>, ResourceType> LockResource;
	static constexpr psize dataAlignment = 4;
protected:
	uint8* data = nullptr;
	psize size = 0, capacity = 16;
	vector<LockResource> lockedResources;
	vector<LockResource> lockingResources;
	uint32 lastSize = 0;
	CommandBufferType type = {};
	bool noSubpass = false;
	bool hasAnyCommand = false;
	bool isRunning = false;

	Command* allocateCommand(uint32 size);
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
	virtual void processCommand(const DrawCommand& command) = 0;
	virtual void processCommand(const DrawIndexedCommand& command) = 0;
	virtual void processCommand(const DispatchCommand& command) = 0;
	virtual void processCommand(const FillBufferCommand& command) = 0;
	virtual void processCommand(const CopyBufferCommand& command) = 0;
	virtual void processCommand(const ClearImageCommand& command) = 0;
	virtual void processCommand(const CopyImageCommand& command) = 0;
	virtual void processCommand(const CopyBufferImageCommand& command) = 0;
	virtual void processCommand(const BlitImageCommand& command) = 0;
	virtual void processCommand(const SetDepthBiasCommand& command) = 0;
	virtual void processCommand(const BuildAccelerationStructureCommand& command) = 0;
	virtual void processCommand(const CopyAccelerationStructureCommand& command) = 0;
	virtual void processCommand(const TraceRaysCommand& command) = 0;
	virtual void processCommand(const CustomRenderCommand& command) = 0;

	#if GARDEN_DEBUG
	virtual void processCommand(const BeginLabelCommand& command) = 0;
	virtual void processCommand(const EndLabelCommand& command) = 0;
	virtual void processCommand(const InsertLabelCommand& command) = 0;
	#endif

	void flushLockedResources(vector<LockResource>& lockedResources);
public:
	/*******************************************************************************************************************
	 * @brief Create a new command buffer instance.
	 */
	CommandBuffer(CommandBufferType type);
	/**
	 * @brief Destroys command buffer instance.
	 */
	virtual ~CommandBuffer();

	/**
	 * @brief Asynchronous command recording mutex.
	 */
	mutex commandMutex;

	/**
	 * @brief Return command buffer type.
	 */
	CommandBufferType getType() const noexcept { return type; }

	void addCommand(const BufferBarrierCommand& command);
	void addCommand(const BeginRenderPassCommand& command);
	void addCommand(const NextSubpassCommand& command);
	void addCommand(const ExecuteCommand& command);
	void addCommand(const EndRenderPassCommand& command);
	void addCommand(const ClearAttachmentsCommand& command);
	void addCommand(const BindPipelineCommand& command);
	void addCommand(const BindDescriptorSetsCommand& command);
	void addCommand(const PushConstantsCommand& command);
	void addCommand(const SetViewportCommand& command);
	void addCommand(const SetScissorCommand& command);
	void addCommand(const SetViewportScissorCommand& command);
	void addCommand(const DrawCommand& command);
	void addCommand(const DrawIndexedCommand& command);
	void addCommand(const DispatchCommand& command);
	void addCommand(const FillBufferCommand& command);
	void addCommand(const CopyBufferCommand& command);
	void addCommand(const ClearImageCommand& command);
	void addCommand(const CopyImageCommand& command);
	void addCommand(const CopyBufferImageCommand& command);
	void addCommand(const BlitImageCommand& command);
	void addCommand(const SetDepthBiasCommand& command);
	void addCommand(const BuildAccelerationStructureCommand& command);
	void addCommand(const CopyAccelerationStructureCommand& command);
	void addCommand(const TraceRaysCommand& command);
	void addCommand(const CustomRenderCommand& command);

	#if GARDEN_DEBUG
	void addCommand(const BeginLabelCommand& command);
	void addCommand(const EndLabelCommand& command);
	void addCommand(const InsertLabelCommand& command);
	#endif

	/**
	 * @brief Submits recorded command to the GPU.
	 */
	virtual void submit() = 0;
	/**
	 * @brief Returns true if command buffer is busy right now.
	 */
	virtual bool isBusy() = 0;

	void addLockedResource(ID<Buffer> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Buffer); }
	void addLockedResource(ID<Image> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Image); }
	void addLockedResource(ID<ImageView> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::ImageView); }
	void addLockedResource(ID<Framebuffer> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Framebuffer); }
	void addLockedResource(ID<Sampler> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Sampler); }
	void addLockedResource(ID<Blas> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Blas); }
	void addLockedResource(ID<Tlas> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Tlas); }
	void addLockedResource(ID<GraphicsPipeline> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::GraphicsPipeline); }
	void addLockedResource(ID<ComputePipeline> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::ComputePipeline); }
	void addLockedResource(ID<RayTracingPipeline> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::RayTracingPipeline); }
	void addLockedResource(ID<DescriptorSet> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::DescriptorSet); }
};

} // namespace garden::graphics