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

#include <mutex>
#include <unordered_map>

namespace garden::graphics
{

struct Command
{
	enum class Type : uint8
	{
		Unknown, BeginRenderPass, NextSubpass, Execute, EndRenderPass, ClearAttachments,
		BindPipeline, BindDescriptorSets, PushConstants, SetViewport, SetScissor,
		SetViewportScissor, Draw, DrawIndexed, Dispatch, // TODO: indirect
		FillBuffer, CopyBuffer, ClearImage, CopyImage, CopyBufferImage, BlitImage,

		#if GARDEN_DEBUG
		BeginLabel, EndLabel, InsertLabel,
		#endif

		Count
	};

	uint32 thisSize = 0;
	uint32 lastSize = 0;
	Type type = {};

	constexpr Command(Type type) noexcept : type(type) { }
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
	int4 region = int4(0);
	constexpr BeginRenderPassCommandBase() noexcept : Command(Type::BeginRenderPass) { }
};
struct BeginRenderPassCommand final : public BeginRenderPassCommandBase
{
	const f32x4* clearColors = nullptr;
};
struct NextSubpassCommand final : public Command
{
	uint8 asyncRecording = false;
	uint16 _alignment = 0;
	constexpr NextSubpassCommand() noexcept : Command(Type::NextSubpass) { }
};
struct ExecuteCommandBase : public Command
{
	uint8 _alignment = 0;
	uint16 bufferCount = 0;
	constexpr ExecuteCommandBase() noexcept : Command(Type::Execute) { }
};
struct ExecuteCommand final : public ExecuteCommandBase
{
	void* buffers = nullptr;
};
struct EndRenderPassCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	constexpr EndRenderPassCommand() noexcept : Command(Type::EndRenderPass) { }
};

//**********************************************************************************************************************
struct ClearAttachmentsCommandBase : public Command
{
	uint8 attachmentCount = 0;
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Framebuffer> framebuffer = {};
	constexpr ClearAttachmentsCommandBase() noexcept : Command(Type::ClearAttachments) { }
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
	constexpr BindPipelineCommand() noexcept : Command(Type::BindPipeline) { }
};
struct BindDescriptorSetsCommandBase : public Command
{
	uint8 asyncRecording = false;
	uint8 rangeCount = 0;
	uint8 _alignment = 0;
	constexpr BindDescriptorSetsCommandBase() noexcept : Command(Type::BindDescriptorSets) { }
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
	constexpr PushConstantsCommandBase() noexcept : Command(Type::PushConstants) { }
};
struct PushConstantsCommand final : public PushConstantsCommandBase
{
	const void* data = nullptr;
};
struct SetViewportCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewport = float4(0.0f);
	constexpr SetViewportCommand() noexcept : Command(Type::SetViewport) { }
};
struct SetScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	int4 scissor = int4(0);
	constexpr SetScissorCommand() noexcept : Command(Type::SetScissor) { }
};
struct SetViewportScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewportScissor = float4(0.0f);
	constexpr SetViewportScissorCommand() noexcept : Command(Type::SetViewportScissor) { }
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
	constexpr DrawCommand() noexcept : Command(Type::Draw) { }
};
struct DrawIndexedCommand final : public Command
{
	GraphicsPipeline::Index indexType = {};
	uint8 asyncRecording = false;
	uint8 _alignment = 0;
	uint32 indexCount = 0;
	uint32 instanceCount = 0;
	uint32 indexOffset = 0;
	uint32 vertexOffset = 0;
	uint32 instanceOffset = 0;
	ID<Buffer> vertexBuffer = {};
	ID<Buffer> indexBuffer = {};
	constexpr DrawIndexedCommand() noexcept : Command(Type::DrawIndexed) { }
};
struct DispatchCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint3 groupCount = uint3(0);
	constexpr DispatchCommand() noexcept : Command(Type::Dispatch) { }
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
	constexpr FillBufferCommand() noexcept : Command(Type::FillBuffer) { }
};
struct CopyBufferCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	uint32 regionCount = 0;
	ID<Buffer> source = {};
	ID<Buffer> destination = {};
	constexpr CopyBufferCommandBase() noexcept : Command(Type::CopyBuffer) { }
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
	float4 color = float4(0);
	constexpr ClearImageCommandBase() noexcept : Command(Type::ClearImage) { }
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
	constexpr CopyImageCommandBase() noexcept : Command(Type::CopyImage) { }
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
	constexpr CopyBufferImageCommandBase() noexcept : Command(Type::CopyBufferImage) { }
};
struct CopyBufferImageCommand final : public CopyBufferImageCommandBase
{
	const Image::CopyBufferRegion* regions = nullptr;
};
struct BlitImageCommandBase : public Command
{
	SamplerFilter filter = {};
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Image> source = {};
	ID<Image> destination = {};
	constexpr BlitImageCommandBase() noexcept : Command(Type::BlitImage) { }
};
struct BlitImageCommand final : public BlitImageCommandBase
{
	const Image::BlitRegion* regions = nullptr;
};

#if GARDEN_DEBUG
//**********************************************************************************************************************
struct BeginLabelCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	Color color = Color((uint8)0);
	constexpr BeginLabelCommandBase() noexcept : Command(Type::BeginLabel) { }
};
struct BeginLabelCommand final : public BeginLabelCommandBase
{
	const char* name = nullptr;
};
struct EndLabelCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	constexpr EndLabelCommand() noexcept : Command(Type::EndLabel) { }
};
struct InsertLabelCommandBase : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	Color color = Color((uint8)0);
	constexpr InsertLabelCommandBase() noexcept : Command(Type::InsertLabel) { }
};
struct InsertLabelCommand final : public InsertLabelCommandBase
{
	const char* name = nullptr;
};
#endif

constexpr psize alignSize(psize size, psize alignment = 4) noexcept
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

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
	struct ImageSubresource final
	{
		ID<Image> image = {};
		uint32 mip = 0;
		uint32 layer = 0;

		bool operator<(ImageSubresource v) const noexcept
		{
			return memcmp(this, &v, sizeof(ImageSubresource)) < 0;
		}
	};
	struct ImageState final
	{
		uint32 access = 0;
		uint32 layout = 0;
		uint32 stage = 0;
	};
	struct BufferState final
	{
		uint32 access = 0;
		uint32 stage = 0;
	};

	typedef pair<ID<Resource>, ResourceType> LockResource;
protected:
	uint8* data = nullptr;
	psize size = 0, capacity = 16;
	map<ImageSubresource, ImageState> imageStates;
	unordered_map<ID<Buffer>, BufferState, IdHash<Buffer>> bufferStates;
	vector<LockResource> lockedResources;
	vector<LockResource> lockingResources;
	uint32 lastSize = 0;
	CommandBufferType type = {};
	bool noSubpass = false;
	bool hasAnyCommand = false;
	bool isRunning = false;

	ImageState& getImageState(ID<Image> image, uint32 mip, uint32 layer);
	BufferState& getBufferState(ID<Buffer> buffer);

	Command* allocateCommand(uint32 size);
	void processCommands();
	void updateImageStates();

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

	#if GARDEN_DEBUG
	void addCommand(const BeginLabelCommand& command);
	void addCommand(const EndLabelCommand& command);
	void addCommand(const InsertLabelCommand& command);
	#endif

	/**
	 * @brief Submits recorded command to the GPU.
	 */
	virtual void submit() = 0;

	void addLockResource(ID<Buffer> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Buffer); }
	void addLockResource(ID<Image> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Image); }
	void addLockResource(ID<ImageView> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::ImageView); }
	void addLockResource(ID<Framebuffer> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::Framebuffer); }
	void addLockResource(ID<GraphicsPipeline> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::GraphicsPipeline); }
	void addLockResource(ID<ComputePipeline> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::ComputePipeline); }
	void addLockResource(ID<DescriptorSet> resource)
	{ lockingResources.emplace_back(ID<Resource>(resource), ResourceType::DescriptorSet); }
};

} // namespace garden::graphics