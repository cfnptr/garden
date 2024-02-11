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
#include "garden/graphics/image.hpp"

namespace garden::graphics
{

using namespace ecsm;

class Pipeline;
class FramebufferExt;

//--------------------------------------------------------------------------------------------------
class Framebuffer final : public Resource
{
public:
	struct InputAttachment final
	{
		ID<ImageView> imageView = {};
		// WARNING: this variable affects syncronization.
		ShaderStage shaderStages = {};

		InputAttachment(ID<ImageView> imageView,
			ShaderStage shaderStages) noexcept
		{
			this->imageView = imageView;
			this->shaderStages = shaderStages;
		}
		InputAttachment() { }
	};
	struct OutputAttachment final
	{
		ID<ImageView> imageView = {};
		// WARNING: these values are per attachment, not subpass.
		bool clear = false, load = false, store = false;

		OutputAttachment(ID<ImageView> imageView,
			bool clear, bool load, bool store) noexcept
		{
			this->imageView = imageView;
			this->clear = clear;
			this->load = load;
			this->store = store;
		}
		OutputAttachment() { }
	};

	struct Subpass final
	{
		vector<InputAttachment> inputAttachments;
		vector<OutputAttachment> outputAttachments;
		PipelineType pipelineType = {};

		Subpass(PipelineType pipelineType,
			const vector<InputAttachment>& inputAttachments,
			const vector<OutputAttachment>& outputAttachments) noexcept
		{
			this->inputAttachments = inputAttachments;
			this->outputAttachments = outputAttachments;
			this->pipelineType = pipelineType;
		}
	};
	struct SubpassImages final
	{
		vector<ID<ImageView>> inputAttachments;
		vector<ID<ImageView>> outputAttachments;
	public:
		SubpassImages(const vector<ID<ImageView>>& inputAttachments,
			const vector<ID<ImageView>>& outputAttachments) noexcept
		{
			this->inputAttachments = inputAttachments;
			this->outputAttachments = outputAttachments;
		}
	};

	struct DepthStencilColor final
	{
		float depth = 0.0f;
		uint32 stencil = 0;
	};
	union ClearColor final
	{
		float4 floatValue = float4(0.0f);
		int4 intValue;
		DepthStencilColor deptStencilValue;
	};
	struct ClearAttachment final
	{
		uint32 index = 0;
		ClearColor color = {};
	};
	struct ClearRegion final
	{
		int2 offset = int2(0);
		int2 extent = int2(0);
		uint32 baseLayer = 0;
		uint32 layerCount = 0;
	};
//--------------------------------------------------------------------------------------------------
private:
	void* renderPass = nullptr;
	vector<Subpass> subpasses;
	vector<OutputAttachment> colorAttachments;
	int2 size = int2(0);
	OutputAttachment depthStencilAttachment = {};
	bool isSwapchain = false;

	Framebuffer() = default;
	Framebuffer(int2 size, vector<Subpass>&& subpasses);
	Framebuffer(int2 size, vector<OutputAttachment>&& colorAttachments,
		OutputAttachment depthStencilAttachment);
	Framebuffer(int2 size, ID<ImageView> swapchainImage)
	{
		this->colorAttachments.push_back(
			OutputAttachment(swapchainImage, false, true, true));
		this->size = size;
		this->isSwapchain = true;
	}
	bool destroy() final;

	static ID<Framebuffer> currentFramebuffer;
	static uint32 currentSubpassIndex;
	static vector<ID<Pipeline>> currentPipelines;
	static vector<PipelineType> currentPipelineTypes;
	static vector<ID<Buffer>> currentVertexBuffers;
	static vector<ID<Buffer>> currentIndexBuffers;

	friend class Pipeline;
	friend class CommandBuffer;
	friend class FramebufferExt;
	friend class GraphicsPipeline;
	friend class LinearPool<Framebuffer>;
//--------------------------------------------------------------------------------------------------
public:
	int2 getSize() const noexcept { return size; }
	const vector<OutputAttachment>& getColorAttachments()
		const noexcept { return colorAttachments; }
	const OutputAttachment& getDepthStencilAttachment()
		const noexcept { return depthStencilAttachment; }
	const vector<Subpass>& getSubpasses() const noexcept { return subpasses; }
	bool isSwapchainFramebuffer() const noexcept { return isSwapchain; }

	void update(int2 size, const OutputAttachment* colorAttachments,
		uint32 colorAttachmentCount, OutputAttachment depthStencilAttachment = {});
	void update(int2 size, vector<OutputAttachment>&& colorAttachments,
		OutputAttachment depthStencilAttachment = {});
	
	void recreate(int2 size, const vector<SubpassImages>& subpasses);

	static ID<Framebuffer> getCurrent() noexcept { return currentFramebuffer; }
	static uint8 getCurrentSubpassIndex() noexcept { return currentSubpassIndex; }
	static bool isCurrentRenderPassAsync() noexcept;

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------
	void beginRenderPass(const float4* clearColors = nullptr,
		uint8 clearColorCount = 0, float clearDepth = 0.0f, uint32 clearStencil = 0x00,
		const int4& region = int4(0), bool recordAsync = false);
	
	template<psize N>
	void beginRenderPass(const array<float4, N>& clearColors,
		float clearDepth = 0.0f, uint32 clearStencil = 0x00,
		const int4& region = int4(0), bool recordAsync = false)
	{
		beginRenderPass(clearColors.data(), (uint8)N,
			clearDepth, clearStencil, region, recordAsync);
	}
	void beginRenderPass(const vector<float4>& clearColors,
		float clearDepth = 0.0f, uint32 clearStencil = 0x00,
		const int4& region = int4(0), bool recordAsync = false)
	{
		beginRenderPass(clearColors.data(), (uint8)clearColors.size(),
			clearDepth, clearStencil, region, recordAsync);
	}
	void beginRenderPass(const float4& clearColor,
		float clearDepth = 0.0f, uint32 clearStencil = 0x00,
		const int4& region = int4(0), bool recordAsync = false)
	{
		beginRenderPass(&clearColor, 1, clearDepth,
			clearStencil, region, recordAsync);
	}
	
	void nextSubpass(bool recordAsync = false);
	void endRenderPass();

	void clearAttachments(const ClearAttachment* attachments, uint8 attachmentCount,
		const ClearRegion* regions, uint32 regionCount);

	template<psize A, psize R>
	void clearAttachments(const array<ClearAttachment, A>& attachments,
		const array<ClearRegion, R>& regions)
	{ clearAttachments(attachments.data(), (uint8)A, regions.data(), (uint32)R); }
	void clearAttachments(const vector<ClearAttachment>& attachments,
		const vector<ClearRegion>& regions)
	{
		clearAttachments(attachments.data(), (uint8)attachments.size(),
			regions.data(), (uint32)regions.size());
	}

	void clearAttachments(ClearAttachment attachment, const ClearRegion& region)
	{ clearAttachments(&attachment, 1, &region, 1); }
	void clearAttachments(ClearAttachment attachment)
	{
		ClearRegion region;
		clearAttachments(&attachment, 1, &region, 1);
	}
};

//--------------------------------------------------------------------------------------------------
class FramebufferExt final
{
public:
	static int2& getSize(Framebuffer& framebuffer)
		noexcept { return framebuffer.size; }
	static vector<Framebuffer::OutputAttachment>& getColorAttachments(
		Framebuffer& framebuffer) noexcept { return framebuffer.colorAttachments; }
	static void*& getRenderPass(Framebuffer& framebuffer)
		noexcept { return framebuffer.renderPass; }
};

} // namespace garden::graphics