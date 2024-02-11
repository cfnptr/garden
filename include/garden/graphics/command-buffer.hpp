//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

namespace garden::graphics
{

using namespace std;
class CommandBuffer;

//--------------------------------------------------------------------------------------------------
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
private:
	uint32 thisSize = 0;
	uint32 lastSize = 0;
	Type type = {};
public:
	friend class CommandBuffer;
	Command(Type type) noexcept { this->type = type; }
};
struct BeginRenderPassCommandBase : public Command
{
	uint8 clearColorCount = 0;
	uint8 recordAsync = false;
	uint8 _alignment = 0;
	ID<Framebuffer> framebuffer = {};
	float clearDepth = 0.0f;
	uint32 clearStencil = 0x00;
	int4 region = int4(0);
	BeginRenderPassCommandBase() noexcept : Command(Type::BeginRenderPass) { }
};
struct BeginRenderPassCommand final : public BeginRenderPassCommandBase
{
	const float4* clearColors = nullptr;
};
struct NextSubpassCommand final : public Command
{
	uint8 recordAsync = false;
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
	uint8 isAsync = false;
	uint8 descriptorDataSize = 0;
	uint8 _alignment = 0;
	BindDescriptorSetsCommandBase() noexcept : Command(Type::BindDescriptorSets) { }
};
struct BindDescriptorSetsCommand final : public BindDescriptorSetsCommandBase
{
	const Pipeline::DescriptorData* descriptorData = nullptr;
};
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
	float4 viewport = float4(0.0f);
	SetViewportCommand() noexcept : Command(Type::SetViewport) { }
};
struct SetScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	int4 scissor = int4(0);
	SetScissorCommand() noexcept : Command(Type::SetScissor) { }
};
struct SetViewportScissorCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	float4 viewportScissor = float4(0.0f);
	SetViewportScissorCommand() noexcept : Command(Type::SetViewportScissor) { }
};
struct DrawCommand final : public Command
{
	uint8 isAsync = false;
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
	GraphicsPipeline::Index indexType = {};
	uint8 isAsync = false;
	uint8 _alignment = 0;
	uint32 indexCount = 0;
	uint32 instanceCount = 0;
	uint32 indexOffset = 0;
	uint32 instanceOffset = 0;
	uint32 vertexOffset = 0;
	ID<Buffer> vertexBuffer = {};
	ID<Buffer> indexBuffer = {};
	DrawIndexedCommand() noexcept : Command(Type::DrawIndexed) { }
};
struct DispatchCommand final : public Command
{
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	int3 groupCount = int3(0);
	DispatchCommand() noexcept : Command(Type::Dispatch) { }
};
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
struct ClearImageCommandBase : public Command
{
	uint8 clearType = 0;
	uint16 _alignment = 0;
	uint32 regionCount = 0;
	ID<Image> image = {};
	float4 color = float4(0.0f);
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
	SamplerFilter filter = {};
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

#if GARDEN_DEBUG
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

//--------------------------------------------------------------------------------------------------
static psize alignSize(psize size, psize alignment = 4) noexcept
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

//--------------------------------------------------------------------------------------------------
class CommandBuffer final
{
public:
	struct ImageSubresource final
	{
		ID<Image> image = {};
		uint32 mip = 0;
		uint32 layer = 0;

		bool operator<(ImageSubresource v) const noexcept {
			return memcmp(this, &v, sizeof(ImageSubresource)) < 0; }
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
private:
	void* instance = nullptr;
	void* fence = nullptr;
	uint8* data = nullptr;
	psize size = 0, capacity = 16;
	map<ImageSubresource, ImageState> imageStates;
	map<ID<Buffer>, BufferState> bufferStates;
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

	void addDescriptorSetBarriers(const Pipeline::DescriptorData* descriptorData,
		uint32 descriptorDataSize, uint32& oldStage, uint32& newStage);
	void processPipelineBarriers(uint32 oldStage, uint32 newStage);

	void processCommand(const BeginRenderPassCommand& command);
	void processCommand(const NextSubpassCommand& command);
	void processCommand(const ExecuteCommand& command);
	void processCommand(const EndRenderPassCommand& command);
	void processCommand(const ClearAttachmentsCommand& command);
	void processCommand(const BindPipelineCommand& command);
	void processCommand(const BindDescriptorSetsCommand& command);
	void processCommand(const PushConstantsCommand& command);
	void processCommand(const SetViewportCommand& command);
	void processCommand(const SetScissorCommand& command);
	void processCommand(const SetViewportScissorCommand& command);
	void processCommand(const DrawCommand& command);
	void processCommand(const DrawIndexedCommand& command);
	void processCommand(const DispatchCommand& command);
	void processCommand(const FillBufferCommand& command);
	void processCommand(const CopyBufferCommand& command);
	void processCommand(const ClearImageCommand& command);
	void processCommand(const CopyImageCommand& command);
	void processCommand(const CopyBufferImageCommand& command);
	void processCommand(const BlitImageCommand& command);

	#if GARDEN_DEBUG
	void processCommand(const BeginLabelCommand& command);
	void processCommand(const EndLabelCommand& command);
	void processCommand(const InsertLabelCommand& command);
	#endif

	void flushLockedResources(vector<LockResource>& lockedResources);
	friend class Vulkan;
public:
//--------------------------------------------------------------------------------------------------
	mutex commandMutex;

	void initialize(CommandBufferType type);
	void terminate();

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

	void submit();

	// DescriptorSet, Pipeline, DescriptorPool, DescriptorSetLayout,
	//	Sampler, Framebuffer, ImageView, Image, Buffer, Count // Note: in destruction order

	void addLockResource(ID<Buffer> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::Buffer)); }
	void addLockResource(ID<Image> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::Image)); }
	void addLockResource(ID<ImageView> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::ImageView)); }
	void addLockResource(ID<Framebuffer> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::Framebuffer)); }
	void addLockResource(ID<GraphicsPipeline> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::GraphicsPipeline)); }
	void addLockResource(ID<ComputePipeline> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::ComputePipeline)); }
	void addLockResource(ID<DescriptorSet> resource)
	{ lockingResources.push_back(make_pair(ID<Resource>(resource), ResourceType::DescriptorSet)); }
};

} // namespace garden::graphics