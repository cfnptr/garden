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

#include "garden/graphics/framebuffer.hpp"
#include "garden/graphics/vulkan.hpp"

#include <set>

using namespace std;
using namespace garden::graphics;

namespace
{
	struct ImageViewState final
	{
		uint32 index = 0;
		uint32 stage = 0;
		uint32 access = 0;
	};
}

ID<Framebuffer> Framebuffer::currentFramebuffer = {};
uint32 Framebuffer::currentSubpassIndex = 0;
vector<ID<Pipeline>> Framebuffer::currentPipelines(thread::hardware_concurrency());
vector<PipelineType> Framebuffer::currentPipelineTypes(thread::hardware_concurrency());
vector<ID<Buffer>> Framebuffer::currentVertexBuffers(thread::hardware_concurrency());
vector<ID<Buffer>> Framebuffer::currentIndexBuffers(thread::hardware_concurrency());

bool Framebuffer::isCurrentRenderPassAsync() noexcept
{
	return !Vulkan::secondaryCommandBuffers.empty();
}

//**********************************************************************************************************************
Framebuffer::Framebuffer(int2 size, vector<Subpass>&& subpasses)
{
	auto subpassCount = (uint32)subpasses.size();
	uint32 referenceCount = 0, referenceOffset = 0;

	for	(const auto& subpass : subpasses)
		referenceCount += (uint32)(subpass.inputAttachments.size() + subpass.outputAttachments.size());

	map<ID<ImageView>, ImageViewState> imageViewStates;
	vector<vk::AttachmentDescription> attachmentDescriptions;
	vector<vk::ImageView> imageViews;
	vector<vk::AttachmentReference> attachmentReferences(referenceCount);
	vector<vk::SubpassDescription> subpassDescriptions(subpassCount);
	vector<vk::SubpassDependency> subpassDependencies(subpassCount - 1);

	for (uint32 i = 0; i < subpassCount; i++)
	{
		const auto& subpass = subpasses[i];
		const auto& inputAttachments = subpass.inputAttachments;
		const auto& outputAttachments = subpass.outputAttachments;
		
		vk::SubpassDescription subpassDescription;
		subpassDescription.pipelineBindPoint = toVkPipelineBindPoint(subpass.pipelineType);
		subpassDescription.inputAttachmentCount = (uint32)inputAttachments.size();
		subpassDescription.pInputAttachments = attachmentReferences.data() + referenceOffset;
		subpassDescription.colorAttachmentCount = (uint32)outputAttachments.size();
		subpassDescription.pColorAttachments =
			attachmentReferences.data() + referenceOffset + inputAttachments.size();

		auto oldDependencyStage = (uint32)vk::PipelineStageFlagBits::eNone;
		auto newDependencyStage = (uint32)vk::PipelineStageFlagBits::eNone;
		auto oldDependencyAccess = (uint32)VK_ACCESS_NONE;
		auto newDependencyAccess = (uint32)VK_ACCESS_NONE;

		for (uint32 j = 0; j < (uint32)inputAttachments.size(); j++)
		{
			auto inputAttachment = inputAttachments[j];
			auto& result = imageViewStates.at(inputAttachment.imageView);

			attachmentReferences[j + referenceOffset] = vk::AttachmentReference(
				result.index, vk::ImageLayout::eShaderReadOnlyOptimal);

			auto newStage = (uint32)toVkPipelineStages(inputAttachment.shaderStages);
			auto newAccess = (uint32)vk::AccessFlagBits::eInputAttachmentRead;
			oldDependencyStage |= result.stage;
			oldDependencyAccess |= result.access;
			newDependencyStage |= newStage;
			newDependencyAccess |= newAccess;
			result.stage = newStage;
			result.access = newAccess;
		}

		referenceOffset += (uint32)inputAttachments.size();

		for (uint32 j = 0; j < (uint32)outputAttachments.size(); j++)
		{
			auto outputAttachment = outputAttachments[j];
			auto imageView = GraphicsAPI::imageViewPool.get(outputAttachment.imageView);
			auto imageFormat = imageView->format;
			auto isImageFormatColor = isFormatColor(imageFormat);
			auto result = imageViewStates.find(outputAttachment.imageView);

			auto imageLayout = isImageFormatColor ?
				vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eDepthStencilAttachmentOptimal;

			if (result != imageViewStates.end())
			{
				attachmentReferences[j + referenceOffset] =
					vk::AttachmentReference(result->second.index, imageLayout);
				
				uint32 newStage, newAccess;

				if (isImageFormatColor)
				{
					newStage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
					newAccess = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
				}
				else
				{
					newStage = (uint32)vk::PipelineStageFlagBits::eLateFragmentTests;
					newAccess = (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				}

				oldDependencyStage |= result->second.stage;
				oldDependencyAccess |= result->second.access;
				newDependencyStage |= newStage;
				newDependencyAccess |= newAccess;
				result->second.stage = newStage;
				result->second.access = newAccess;

				if (!isImageFormatColor)
				{
					subpassDescription.colorAttachmentCount--;
					subpassDescription.pDepthStencilAttachment = &attachmentReferences[j + referenceOffset];
				}

				// TODO: check if clear/load/store are the same as in first declaration.
				continue;
			}

			attachmentReferences[j + referenceOffset] = vk::AttachmentReference(
				(uint32)attachmentDescriptions.size(), imageLayout);

			vk::AttachmentDescription attachmentDescription;
			attachmentDescription.format = toVkFormat(imageFormat);
			attachmentDescription.samples = vk::SampleCountFlagBits::e1;
			attachmentDescription.initialLayout = imageLayout;
			attachmentDescription.finalLayout = vk::ImageLayout::eGeneral;
			
			if (isImageFormatColor)
			{
				if (outputAttachment.clear)
					attachmentDescription.loadOp = vk::AttachmentLoadOp::eClear;
				else if (outputAttachment.load)
					attachmentDescription.loadOp = vk::AttachmentLoadOp::eLoad;
				else
					attachmentDescription.loadOp = vk::AttachmentLoadOp::eDontCare;
				
				if (outputAttachment.store)
					attachmentDescription.storeOp = vk::AttachmentStoreOp::eStore;
				else
					attachmentDescription.storeOp = vk::AttachmentStoreOp::eDontCare;

				attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
				attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
				this->colorAttachments.push_back(outputAttachment);
			}
			else
			{
				if (isFormatDepthOnly(imageFormat))
				{
					if (outputAttachment.clear)
						attachmentDescription.loadOp = vk::AttachmentLoadOp::eClear;
					else if (outputAttachment.load)
						attachmentDescription.loadOp = vk::AttachmentLoadOp::eLoad;
					else
						attachmentDescription.loadOp = vk::AttachmentLoadOp::eDontCare;
					
					if (outputAttachment.store)
						attachmentDescription.storeOp = vk::AttachmentStoreOp::eStore;
					else
						attachmentDescription.storeOp = vk::AttachmentStoreOp::eDontCare;

					attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
					attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
				}
				else if (isFormatStencilOnly(imageFormat))
				{
					attachmentDescription.loadOp = vk::AttachmentLoadOp::eDontCare;
					attachmentDescription.storeOp = vk::AttachmentStoreOp::eDontCare;

					if (outputAttachment.clear)
						attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eClear;
					else if (outputAttachment.load)
						attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eLoad;
					else
						attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;

					if (outputAttachment.store)
						attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eStore;
					else
						attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
				}
				else
				{
					if (outputAttachment.clear) // TODO: support separated depth/stencil clear?
					{
						attachmentDescription.loadOp = attachmentDescription.stencilLoadOp =
							vk::AttachmentLoadOp::eClear;
					}
					else if (outputAttachment.load)
					{
						attachmentDescription.loadOp = attachmentDescription.stencilLoadOp =
							vk::AttachmentLoadOp::eLoad;
					}
					else
					{
						attachmentDescription.loadOp = attachmentDescription.stencilLoadOp =
							vk::AttachmentLoadOp::eDontCare;
					}

					if (outputAttachment.store)
					{
						attachmentDescription.storeOp = attachmentDescription.stencilStoreOp =
							vk::AttachmentStoreOp::eStore;
					}
					else
					{
						attachmentDescription.storeOp = attachmentDescription.stencilStoreOp =
							vk::AttachmentStoreOp::eDontCare;
					}
				}

				subpassDescription.colorAttachmentCount--;
				subpassDescription.pDepthStencilAttachment = &attachmentReferences[j + referenceOffset];
				
				if (!this->depthStencilAttachment.imageView)
				{
					this->depthStencilAttachment = outputAttachment;
				}
				else
				{
					GARDEN_ASSERT(outputAttachment.imageView == this->depthStencilAttachment.imageView);
				}
			}

			ImageViewState imageViewState;
			imageViewState.index = (uint32)attachmentDescriptions.size();

			if (isImageFormatColor)
			{
				imageViewState.stage = (uint32)vk::PipelineStageFlagBits::eColorAttachmentOutput;
				imageViewState.access = (uint32)vk::AccessFlagBits::eColorAttachmentWrite;
			}
			else
			{
				imageViewState.stage = (uint32)vk::PipelineStageFlagBits::eLateFragmentTests;
				imageViewState.access = (uint32)vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			}
			
			imageViewStates.emplace(outputAttachment.imageView, imageViewState);
			attachmentDescriptions.push_back(attachmentDescription);
			imageViews.push_back((VkImageView)imageView->instance);
		}

		referenceOffset += (uint32)outputAttachments.size();

		if (i > 0)
		{
			// TODO: check if there is no redundant dependencies in complex render passes.
			vk::SubpassDependency subpassDependency(i - 1, i,
				vk::PipelineStageFlags(oldDependencyStage), vk::PipelineStageFlags(newDependencyStage),
				vk::AccessFlags(oldDependencyAccess), vk::AccessFlags(newDependencyAccess),
				vk::DependencyFlagBits::eByRegion);
			subpassDependencies[i - 1] = subpassDependency;
		}

		subpassDescriptions[i] = subpassDescription;
	}

	// Note: required for loadOp/storeOp.
	if (!subpassDependencies.empty())
	{
		auto firstDependency = subpassDependencies.begin();
		auto lastDependency = subpassDependencies.rbegin();
		if (!colorAttachments.empty())
		{
			firstDependency->srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
			firstDependency->srcAccessMask |= vk::AccessFlagBits::eColorAttachmentRead;
			lastDependency->dstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
			lastDependency->dstAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
		}
		if (depthStencilAttachment.imageView)
		{
			firstDependency->srcStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests;
			firstDependency->srcAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentRead;
			lastDependency->dstStageMask |= vk::PipelineStageFlagBits::eLateFragmentTests;
			lastDependency->dstAccessMask |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}
	}

	vk::RenderPassCreateInfo renderPassInfo({},
		attachmentDescriptions, subpassDescriptions, subpassDependencies);
	auto renderPass = Vulkan::device.createRenderPass(renderPassInfo);
	this->renderPass = (VkRenderPass)renderPass;
	GraphicsAPI::renderPasses.emplace((VkRenderPass)renderPass, 0);

	vk::FramebufferCreateInfo framebufferInfo({},
		renderPass, imageViews, (uint32)size.x, (uint32)size.y, 1);
	auto framebuffer = Vulkan::device.createFramebuffer(framebufferInfo);
	this->instance = (VkFramebuffer)framebuffer;

	this->subpasses = std::move(subpasses);
	this->size = size;
	this->isSwapchain = false;
}

//**********************************************************************************************************************
Framebuffer::Framebuffer(int2 size, vector<OutputAttachment>&& colorAttachments,
	OutputAttachment depthStencilAttachment)
{
	if (!Vulkan::hasDynamicRendering) // TODO: handle this case and use subpass framebuffer.
		throw runtime_error("Dynamic rendering is not supported on this GPU.");

	this->instance = (void*)1;
	this->colorAttachments = std::move(colorAttachments);
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
	this->isSwapchain = false;
}
bool Framebuffer::destroy()
{
	if (!instance || readyLock > 0 || subpasses.empty())
		return !colorAttachments.empty() || depthStencilAttachment.imageView;

	if (GraphicsAPI::isRunning)
	{
		void* destroyRenderPass = nullptr;

		if (renderPass)
		{
			auto& shareCount = GraphicsAPI::renderPasses.at(renderPass);
			if (shareCount == 0)
			{
				destroyRenderPass = renderPass;
				GraphicsAPI::renderPasses.erase(renderPass);
			}
			else
			{
				shareCount--;
			}
		}

		GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer, instance, destroyRenderPass);
	}
	else
	{
		Vulkan::device.destroyFramebuffer((VkFramebuffer)instance);
		Vulkan::device.destroyRenderPass((VkRenderPass)renderPass);
	}

	instance = nullptr;
	return true;
}

//**********************************************************************************************************************
void Framebuffer::update(int2 size, const OutputAttachment* colorAttachments,
	uint32 colorAttachmentCount, OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(subpasses.empty());
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(colorAttachmentCount > 0 || depthStencilAttachment.imageView);

	#if GARDEN_DEBUG
	for	(uint32 i = 0; i < colorAttachmentCount; i ++)
	{
		const auto& colorAttachment = colorAttachments[i];
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->format));
		auto image = GraphicsAPI::imagePool.get(imageView->image);
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->baseMip));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}

	if (depthStencilAttachment.imageView)
	{
		auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->format));
		auto image = GraphicsAPI::imagePool.get(imageView->image);
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->baseMip));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	if (this->colorAttachments.size() != colorAttachmentCount)
		this->colorAttachments.resize(colorAttachmentCount);
	memcpy(this->colorAttachments.data(), colorAttachments,
		colorAttachmentCount * sizeof(Framebuffer::OutputAttachment));
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
}

//**********************************************************************************************************************
void Framebuffer::update(int2 size, vector<OutputAttachment>&& colorAttachments,
	OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(subpasses.empty());
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);

	#if GARDEN_DEBUG
	for	(const auto& colorAttachment : colorAttachments)
	{
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = GraphicsAPI::imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->format));
		auto image = GraphicsAPI::imagePool.get(imageView->image);
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->baseMip));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}

	if (depthStencilAttachment.imageView)
	{
		auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->format));
		auto image = GraphicsAPI::imagePool.get(imageView->image);
		GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->baseMip));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	this->colorAttachments = std::move(colorAttachments);
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
}

//**********************************************************************************************************************
void Framebuffer::recreate(int2 size, const vector<SubpassImages>& subpasses)
{
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(subpasses.size() == this->subpasses.size());

	set<ID<ImageView>> attachments;
	auto imageViewsCapacity = this->colorAttachments.size();
	if (this->depthStencilAttachment.imageView)
		imageViewsCapacity++;
	vector<vk::ImageView> imageViews(imageViewsCapacity);
	uint32 colorAttachmentIndex = 0, imageViewIndex = 0;
	this->depthStencilAttachment.imageView = {};

	for (uint32 i = 0; i < (uint32)subpasses.size(); i++)
	{
		auto& newSubpass = subpasses[i];
		auto& oldSubpass = this->subpasses[i];
		auto& newInputAttachments = newSubpass.inputAttachments;
		auto& oldInputAttachments = oldSubpass.inputAttachments;
		auto& newOutputAttachments = newSubpass.outputAttachments;
		auto& oldOutputAttachments = oldSubpass.outputAttachments;
		GARDEN_ASSERT(newInputAttachments.size() == oldInputAttachments.size());
		GARDEN_ASSERT(newOutputAttachments.size() == oldOutputAttachments.size());

		for (uint32 j = 0; j < (uint32)newInputAttachments.size(); j++)
		{
			auto newInputAttachment = newInputAttachments[j];
			GARDEN_ASSERT(newInputAttachment);
			auto& oldInputAttachment = oldInputAttachments[j];
			oldInputAttachment.imageView = newInputAttachment;

			#if GARDEN_DEBUG
			auto imageView = GraphicsAPI::imageViewPool.get(newInputAttachment);
			auto image = GraphicsAPI::imagePool.get(imageView->image);
			GARDEN_ASSERT(size == calcSizeAtMip((int2)image->getSize(), imageView->baseMip));
			GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::InputAttachment));
			auto result = attachments.find(newInputAttachment);
			GARDEN_ASSERT(result != attachments.end());
			#endif
		}

		for (uint32 j = 0; j < (uint32)newOutputAttachments.size(); j++)
		{
			auto newOutputAttachment = newOutputAttachments[j];
			GARDEN_ASSERT(newOutputAttachment);
			auto& oldOutputAttachment = oldOutputAttachments[j];
			oldOutputAttachment.imageView = newOutputAttachment;

			auto result = attachments.find(newOutputAttachment);
			if (result != attachments.end())
				continue;

			auto newImageView = GraphicsAPI::imageViewPool.get(newOutputAttachment);
			#if GARDEN_DEBUG
			auto oldImageView = GraphicsAPI::imageViewPool.get(oldOutputAttachment.imageView);
			GARDEN_ASSERT(newImageView->format == oldImageView->format);
			auto newImage = GraphicsAPI::imagePool.get(newImageView->image);
			GARDEN_ASSERT(size == (int2)newImage->getSize());
			#endif

			if (isFormatColor(newImageView->format))
			{
				GARDEN_ASSERT(hasAnyFlag(newImage->getBind(), Image::Bind::ColorAttachment));
				auto index = colorAttachmentIndex++;
				this->colorAttachments[index].imageView = newOutputAttachment;
			}
			else
			{
				GARDEN_ASSERT(hasAnyFlag(newImage->getBind(), Image::Bind::DepthStencilAttachment));
				GARDEN_ASSERT(!this->depthStencilAttachment.imageView);
				this->depthStencilAttachment.imageView = newOutputAttachment;
			}

			imageViews[imageViewIndex++] = vk::ImageView((VkImageView)newImageView->instance);
			attachments.emplace(newOutputAttachment);
		}
	}

	GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer, instance);

	vk::FramebufferCreateInfo framebufferInfo({}, (VkRenderPass)renderPass,
		imageViews, (uint32)size.x, (uint32)size.y, 1);
	auto framebuffer = Vulkan::device.createFramebuffer(framebufferInfo);
	this->instance = (VkFramebuffer)framebuffer;
	this->size = size;
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void Framebuffer::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (!Vulkan::hasDebugUtils || !instance || subpasses.empty())
		return;

	vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eFramebuffer, (uint64)instance, name.c_str());
	Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
	nameInfo.objectType = vk::ObjectType::eRenderPass;
	nameInfo.objectHandle = (uint64)renderPass;
	Vulkan::device.setDebugUtilsObjectNameEXT(nameInfo, Vulkan::dynamicLoader);
}
#endif

//**********************************************************************************************************************
void Framebuffer::beginRenderPass(const float4* clearColors, uint8 clearColorCount,
	float clearDepth, uint32 clearStencil, const int4& region, bool asyncRecording)
{
	GARDEN_ASSERT(!currentFramebuffer);
	GARDEN_ASSERT(clearColorCount == colorAttachments.size());
	GARDEN_ASSERT(!clearColors || (clearColors && clearColorCount > 0));
	GARDEN_ASSERT(region.x + region.z <= size.x);
	GARDEN_ASSERT(region.y + region.w <= size.y);
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	
	currentFramebuffer = GraphicsAPI::framebufferPool.getID(this);
	currentSubpassIndex = 0;

	if (asyncRecording)
	{
		#if GARDEN_DEBUG
		auto name = debugName;
		#else
		auto name = "";
		#endif
		
		Vulkan::swapchain.beginSecondaryCommandBuffers(subpasses.empty() ? nullptr : instance,
			renderPass, currentSubpassIndex, colorAttachments, depthStencilAttachment, name);
			
		for (uint32 i = 0; i < (uint32)Vulkan::secondaryCommandBuffers.size(); i++)
		{
			currentPipelines[i] = {}; currentPipelineTypes[i] = {};
			currentVertexBuffers[i] = currentIndexBuffers[i] = {};
		}
	}

	BeginRenderPassCommand command;
	command.clearColorCount = clearColorCount;
	command.asyncRecording = asyncRecording;
	command.framebuffer = GraphicsAPI::framebufferPool.getID(this);
	command.clearDepth = clearDepth;
	command.clearStencil = clearStencil;
	command.region = region == 0 ? int4(0, 0, size.x, size.y) : region;
	command.clearColors = clearColors;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
	{
		readyLock++;
		GraphicsAPI::currentCommandBuffer->addLockResource(command.framebuffer);
	}
}

//**********************************************************************************************************************
void Framebuffer::nextSubpass(bool asyncRecording)
{
	GARDEN_ASSERT(currentFramebuffer == GraphicsAPI::framebufferPool.getID(this));
	GARDEN_ASSERT(currentSubpassIndex + 1 < subpasses.size());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	if (!Vulkan::secondaryCommandBuffers.empty())
		Vulkan::swapchain.endSecondaryCommandBuffers();
		
	if (asyncRecording)
	{
		#if GARDEN_DEBUG
		auto name = debugName;
		#else
		auto name = "";
		#endif

		Vulkan::swapchain.beginSecondaryCommandBuffers(subpasses.empty() ? nullptr : instance,
			renderPass, currentSubpassIndex + 1, colorAttachments, depthStencilAttachment, name);
		for (uint32 i = 0; i < (uint32)Vulkan::secondaryCommandBuffers.size(); i++)
			currentVertexBuffers[i] = currentIndexBuffers[i] = {};
	}

	NextSubpassCommand command;
	command.asyncRecording = asyncRecording;
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	currentSubpassIndex++;
}
void Framebuffer::endRenderPass()
{
	GARDEN_ASSERT(currentFramebuffer == GraphicsAPI::framebufferPool.getID(this));
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	if (!Vulkan::secondaryCommandBuffers.empty())
		Vulkan::swapchain.endSecondaryCommandBuffers();

	EndRenderPassCommand command;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	currentSubpassIndex = 0;
	currentFramebuffer = {};
}

//**********************************************************************************************************************
void Framebuffer::clearAttachments(const ClearAttachment* attachments,
	uint8 attachmentCount, const ClearRegion* regions, uint32 regionCount)
{
	GARDEN_ASSERT(currentFramebuffer == GraphicsAPI::framebufferPool.getID(this));
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	for (uint32 i = 0; i < regionCount; i++)
	{
		const auto& region = regions[i];
		GARDEN_ASSERT(region.offset >= 0);
		GARDEN_ASSERT(region.extent >= 0);
	}
	#endif

	ClearAttachmentsCommand command;
	command.attachmentCount = attachmentCount;
	command.regionCount = regionCount;
	command.framebuffer = GraphicsAPI::framebufferPool.getID(this);
	command.attachments = attachments;
	command.regions = regions;
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}