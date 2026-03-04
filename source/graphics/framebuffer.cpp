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

#include "garden/graphics/framebuffer.hpp"
#include "garden/graphics/vulkan/api.hpp"

#include <set>

using namespace math;
using namespace garden;
using namespace garden::graphics;

namespace garden::graphics
{
	struct ImageViewState final
	{
		uint32 index = 0;
		uint32 stage = 0;
		uint32 access = 0;
	};
}

static void validateAttachments(uint2 size, const Framebuffer::Attachment* colorAttachments,
	uint32 colorAttachmentCount, Framebuffer::Attachment depthStencilAttachment, const string& debugName)
{
	// TODO: add checks if attachments do not overlaps and repeat.
	// TODO: we can use attachments with different sizes, but should we?

	#if GARDEN_DEBUG
	auto graphicsAPI = GraphicsAPI::get();
	for	(uint32 i = 0; i < colorAttachmentCount; i++)
	{
		const auto& colorAttachment = colorAttachments[i];
		if (!colorAttachment.imageView)
			continue;

		auto imageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT_MSG(isFormatColor(imageView->getFormat()), "Incorrect framebuffer [" + debugName + "] "
			"color attachment [" + to_string(i) + "] image view [" + imageView->getDebugName() + "] format");
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect "
			"framebuffer [" + debugName + "] color attachment [" + to_string(i) + "] "
			"image view [" + imageView->getDebugName() + "] size at mip");
		GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::ColorAttachment), "Missing "
			"framebuffer [" + debugName + "] color attachment [" + to_string(i) + "] "
			"image view [" + imageView->getDebugName() + "] flag");
	}

	if (depthStencilAttachment.imageView)
	{
		auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT_MSG(isFormatDepthOrStencil(imageView->getFormat()), "Incorrect framebuffer [" + 
			debugName + "] depth/stencil attachment image view [" + imageView->getDebugName() + "] format");
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT_MSG(size == imageView->calcSize(), "Incorrect "
			"framebuffer [" + debugName + "] depth/stencil attachment "
			"image view [" + imageView->getDebugName() + "] size at mip");
		GARDEN_ASSERT_MSG(hasAnyFlag(image->getUsage(), Image::Usage::DepthStencilAttachment), 
			"Missing framebuffer [" + debugName + "] depth/stencil attachment "
			"image view [" + imageView->getDebugName() + "] flag");
	}
	#endif
}

static uint32 getDepthStencilLayout(Framebuffer::Attachment depthStencilAttachment) noexcept
{
	if (!depthStencilAttachment.imageView)
		return 0;

	auto graphicsAPI = GraphicsAPI::get();
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto imageFormat = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView)->getFormat();
		if (isFormatDepthOnly(imageFormat))
		{
			return (uint32)(depthStencilAttachment.storeOperation == Framebuffer::StoreOp::Store ? 
				vk::ImageLayout::eDepthAttachmentOptimal : vk::ImageLayout::eDepthReadOnlyOptimal);
			
		}
		else if (isFormatStencilOnly(imageFormat))
		{
			return (uint32)(depthStencilAttachment.storeOperation == Framebuffer::StoreOp::Store ? 
				vk::ImageLayout::eStencilAttachmentOptimal : vk::ImageLayout::eStencilReadOnlyOptimal);
		}
		else
		{
			// TODO: also support separate dept/stencil readonly states.
			return (uint32)(depthStencilAttachment.storeOperation == Framebuffer::StoreOp::Store ? 
				vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eDepthStencilReadOnlyOptimal);
		}
	}
	else abort();
}

Framebuffer::Framebuffer(uint2 size, vector<Attachment>&& colorAttachments, Attachment depthStencilAttachment)
{
	validateAttachments(size, colorAttachments.data(), colorAttachments.size(), depthStencilAttachment, debugName);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		if (!VulkanAPI::get()->features.dynamicRendering)
			throw GardenError("Dynamic rendering is not supported on this GPU.");
	}
	else abort();

	this->colorAttachments = std::move(colorAttachments);
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
	this->depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
	this->isSwapchain = false;
}

//**********************************************************************************************************************
void Framebuffer::update(uint2 size, const Attachment* colorAttachments,
	uint32 colorAttachmentCount, Attachment depthStencilAttachment)
{
	GARDEN_ASSERT_MSG(areAllTrue(size > uint2::zero), "Assert " + debugName);
	GARDEN_ASSERT_MSG(colorAttachmentCount > 0 || depthStencilAttachment.imageView, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	validateAttachments(size, colorAttachments, colorAttachmentCount, depthStencilAttachment, debugName);

	if (this->colorAttachments.size() != colorAttachmentCount)
		this->colorAttachments.resize(colorAttachmentCount);

	if (colorAttachmentCount > 0)
	{
		memcpy(this->colorAttachments.data(), colorAttachments,
		colorAttachmentCount * sizeof(Framebuffer::Attachment));
	}

	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
	this->depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}
void Framebuffer::update(uint2 size, vector<Attachment>&& colorAttachments, Attachment depthStencilAttachment)
{
	GARDEN_ASSERT_MSG(areAllTrue(size > uint2::zero), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!colorAttachments.empty() || depthStencilAttachment.imageView, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	validateAttachments(size, colorAttachments.data(), colorAttachments.size(), depthStencilAttachment, debugName);

	this->colorAttachments = std::move(colorAttachments);
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
	this->depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}

void Framebuffer::update(uint2 size, const ID<ImageView>* colorImageViews,
	uint32 colorImageViewCount, ID<ImageView> depthStencilIV)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(areAllTrue(size > uint2::zero), "Assert " + debugName);
	GARDEN_ASSERT_MSG(colorImageViewCount > 0 || depthStencilIV, "Assert " + debugName);
	GARDEN_ASSERT_MSG(colorImageViewCount == colorAttachments.size(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!graphicsAPI->currentFramebuffer, "Assert " + debugName);
	validateAttachments(size, colorAttachments.data(), colorAttachments.size(), depthStencilAttachment, debugName);

	auto colorAttachmentData = colorAttachments.data();
	for (uint32 i = 0; i < colorImageViewCount; i++)
	{
		auto& colorAttachment = colorAttachmentData[i];
		#if GARDEN_DEBUG
		if (colorAttachment.imageView)
		{
			auto colorImageView = colorImageViews[i];
			GARDEN_ASSERT_MSG(colorImageView, "Assert " + debugName);
			auto oldImageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
			auto newImageView = graphicsAPI->imageViewPool.get(colorImageView);
			GARDEN_ASSERT_MSG(oldImageView->getFormat() == newImageView->getFormat(), "Assert " + debugName);
		}
		#endif
		colorAttachment.imageView = colorImageViews[i];
	}

	this->size = size;
	depthStencilAttachment.imageView = depthStencilIV;
	depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}

void Framebuffer::updateColor(uint32 index, const Attachment& colorAttachment)
{
	GARDEN_ASSERT_MSG(index < colorAttachments.size(), "Assert " + debugName);
	colorAttachments.data()[index] = colorAttachment;
}
void Framebuffer::updateColor(uint32 index, ID<ImageView> colorImageView)
{
	GARDEN_ASSERT_MSG(index < colorAttachments.size(), "Assert " + debugName);
	auto& colorAttachment = colorAttachments.data()[index];

	#if GARDEN_DEBUG
	if (colorAttachment.imageView)
	{
		auto graphicsAPI = GraphicsAPI::get();
		GARDEN_ASSERT_MSG(colorImageView, "Assert " + debugName);
		auto oldImageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		auto newImageView = graphicsAPI->imageViewPool.get(colorImageView);
		GARDEN_ASSERT_MSG(oldImageView->getFormat() == newImageView->getFormat(), "Assert " + debugName);
	}
	#endif
	colorAttachment.imageView = colorImageView;
}
void Framebuffer::updateColor(uint32 index, LoadOp loadOperation, StoreOp storeOperation)
{
	auto& colorAttachment = colorAttachments.data()[index];
	colorAttachment.loadOperation = loadOperation;
	colorAttachment.storeOperation = storeOperation;
}

void Framebuffer::updateDepthStencil(const Attachment& depthStencilAttachment)
{
	this->depthStencilAttachment = depthStencilAttachment;
	this->depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}
void Framebuffer::updateDepthStencil(ID<ImageView> depthStencilIV)
{
	if (depthStencilAttachment.imageView == depthStencilIV)
		return;

	#if GARDEN_DEBUG
	if (depthStencilAttachment.imageView)
	{
		auto graphicsAPI = GraphicsAPI::get();
		GARDEN_ASSERT_MSG(depthStencilIV, "Assert " + debugName);
		auto oldImageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		auto newImageView = graphicsAPI->imageViewPool.get(depthStencilIV);
		GARDEN_ASSERT_MSG(oldImageView->getFormat() == newImageView->getFormat(), "Assert " + debugName);
	}
	#endif

	depthStencilAttachment.imageView = depthStencilIV;
	depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}
void Framebuffer::updateDepthStencil(LoadOp loadOperation, StoreOp storeOperation)
{
	if (depthStencilAttachment.loadOperation == loadOperation &&
		depthStencilAttachment.storeOperation == storeOperation)
	{
		return;
	}
	depthStencilAttachment.loadOperation = loadOperation;
	depthStencilAttachment.storeOperation = storeOperation;
	depthStencilLayout = getDepthStencilLayout(depthStencilAttachment);
}

//**********************************************************************************************************************
void Framebuffer::beginRenderPass(const float4* clearColors, uint8 clearColorCount,
	float clearDepth, uint32 clearStencil, int4 region, bool asyncRecording)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(!graphicsAPI->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(clearColorCount == colorAttachments.size(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(!clearColors || (clearColors && clearColorCount > 0), "Assert " + debugName);
	GARDEN_ASSERT_MSG(region.x + region.z <= size.x, "Assert " + debugName);
	GARDEN_ASSERT_MSG(region.y + region.w <= size.y, "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + debugName);

	graphicsAPI->currentFramebuffer = graphicsAPI->framebufferPool.getID(this);

	if (asyncRecording)
	{
		if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			#if GARDEN_DEBUG
			const auto& name = debugName;
			#else
			string name;
			#endif

			VulkanAPI::get()->vulkanSwapchain->beginSecondaryCommandBuffers(
				colorAttachments, depthStencilAttachment, name);
		}
		else abort();

		auto threadCount = graphicsAPI->getThreadPool()->getThreadCount();
		for (uint32 i = 0; i < threadCount; i++)
		{
			graphicsAPI->currentPipelines[i] = {};
			graphicsAPI->currentPipelineTypes[i] = {};
			graphicsAPI->currentPipelineVariants[i] = 0;
			graphicsAPI->currentVertexBuffers[i] = {};
			graphicsAPI->currentIndexBuffers[i] = {};
		}
	}
	else
	{
		graphicsAPI->currentPipelines[0] = {};
		graphicsAPI->currentPipelineTypes[0] = {}; 
		graphicsAPI->currentPipelineVariants[0] = 0;
		graphicsAPI->currentVertexBuffers[0] = {};
		graphicsAPI->currentIndexBuffers[0] = {};
	}

	BeginRenderPassCommand command;
	command.asyncRecording = asyncRecording;
	command.clearColorCount = clearColorCount;
	command.framebuffer = graphicsAPI->framebufferPool.getID(this);
	command.clearDepth = clearDepth;
	command.clearStencil = clearStencil;
	command.region = region == int4::zero ? int4(0, 0, size.x, size.y) : region;
	command.clearColors = clearColors;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	graphicsAPI->isCurrentRenderPassAsync = asyncRecording;
}
void Framebuffer::endRenderPass()
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(graphicsAPI->currentFramebuffer == 
		graphicsAPI->framebufferPool.getID(this), "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + debugName);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->secondaryCommandBuffers.empty())
			vulkanAPI->vulkanSwapchain->endSecondaryCommandBuffers();
	}
	else abort();

	EndRenderPassCommand command;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	graphicsAPI->isCurrentRenderPassAsync = false;
	graphicsAPI->currentFramebuffer = {};
}

//**********************************************************************************************************************
void Framebuffer::clearAttachments(const ClearAttachment* attachments,
	uint8 attachmentCount, const ClearRegion* regions, uint32 regionCount)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT_MSG(graphicsAPI->currentFramebuffer == 
		graphicsAPI->framebufferPool.getID(this), "Assert " + debugName);
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + debugName);

	ClearAttachmentsCommand command;
	command.attachmentCount = attachmentCount;
	command.regionCount = regionCount;
	command.framebuffer = graphicsAPI->framebufferPool.getID(this);
	command.attachments = attachments;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}

RenderPass::RenderPass(ID<Framebuffer> framebuffer, const float4* clearColors, 
	uint8 clearColorCount, float clearDepth, uint32 clearStencil, int4 region, bool asyncRecording)
{
	GARDEN_ASSERT(framebuffer);
	auto framebufferView = GraphicsAPI::get()->framebufferPool.get(framebuffer);
	framebufferView->beginRenderPass(clearColors, clearColorCount, clearDepth, clearStencil, region, asyncRecording);
	this->framebuffer = framebuffer;
}
RenderPass::~RenderPass()
{
	GARDEN_ASSERT(framebuffer);
	auto framebufferView = GraphicsAPI::get()->framebufferPool.get(framebuffer);
	framebufferView->endRenderPass();
}