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
#include "garden/graphics/vulkan/api.hpp"
#include "garden/profiler.hpp"

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

//**********************************************************************************************************************
static vk::RenderPass createVkRenderPass(VulkanAPI* vulkanAPI, uint2 size,
	const vector<Framebuffer::Subpass>& subpasses, vector<Framebuffer::OutputAttachment>& colorAttachments, 
	Framebuffer::OutputAttachment& depthStencilAttachment, vector<vk::ImageView>& imageViews)
{
	auto subpassCount = (uint32)subpasses.size();
	uint32 referenceCount = 0, referenceOffset = 0;

	for	(const auto& subpass : subpasses)
		referenceCount += (uint32)(subpass.inputAttachments.size() + subpass.outputAttachments.size());

	map<ID<ImageView>, ImageViewState> imageViewStates;
	vector<vk::AttachmentDescription> attachmentDescriptions;
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
			auto imageView = vulkanAPI->imageViewPool.get(outputAttachment.imageView);
			auto imageFormat = imageView->getFormat();
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
				colorAttachments.push_back(outputAttachment);
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
				
				if (!depthStencilAttachment.imageView)
				{
					depthStencilAttachment = outputAttachment;
				}
				else
				{
					GARDEN_ASSERT(outputAttachment.imageView == depthStencilAttachment.imageView);
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
			imageViews.push_back((VkImageView)ResourceExt::getInstance(**imageView));
		}

		referenceOffset += (uint32)outputAttachments.size();

		if (i > 0)
		{
			// TODO: check if there is no redundant dependencies in complex render passes.
			vk::SubpassDependency subpassDependency(i - 1, i, vk::PipelineStageFlags(oldDependencyStage), 
				vk::PipelineStageFlags(newDependencyStage), vk::AccessFlags(oldDependencyAccess), 
				vk::AccessFlags(newDependencyAccess), vk::DependencyFlagBits::eByRegion);
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

	vk::RenderPassCreateInfo renderPassInfo({}, attachmentDescriptions, subpassDescriptions, subpassDependencies);
	return vulkanAPI->device.createRenderPass(renderPassInfo);
}

//**********************************************************************************************************************
static void destroyVkFramebuffer(void* instance, void* renderPass)
{
	auto vulkanAPI = VulkanAPI::get();
	if (vulkanAPI->forceResourceDestroy)
	{
		vulkanAPI->device.destroyFramebuffer((VkFramebuffer)instance);
		vulkanAPI->device.destroyRenderPass((VkRenderPass)renderPass);
	}
	else
	{
		void* destroyRenderPass = nullptr;

		if (renderPass)
		{
			auto& shareCount = vulkanAPI->renderPasses.at(renderPass);
			if (shareCount == 0)
			{
				destroyRenderPass = renderPass;
				vulkanAPI->renderPasses.erase(renderPass);
			}
			else
			{
				shareCount--;
			}
		}

		vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer, instance, destroyRenderPass);
	}
}

//**********************************************************************************************************************
static void recreateVkFramebuffer(uint2 size, const vector<Framebuffer::SubpassImages>& newSubpasses,
	vector<Framebuffer::Subpass>& oldSubpasses, vector<Framebuffer::OutputAttachment>& colorAttachments, 
	Framebuffer::OutputAttachment& depthStencilAttachment, void* renderPass, void*& instance)
{
	auto vulkanAPI = VulkanAPI::get();
	set<ID<ImageView>> attachments;

	auto imageViewsCapacity = colorAttachments.size();
	if (depthStencilAttachment.imageView)
		imageViewsCapacity++;

	vector<vk::ImageView> imageViews(imageViewsCapacity);
	uint32 colorAttachmentIndex = 0, imageViewIndex = 0;
	depthStencilAttachment.imageView = {};

	for (uint32 i = 0; i < (uint32)newSubpasses.size(); i++)
	{
		const auto& newSubpass = newSubpasses[i];
		auto& oldSubpass = oldSubpasses[i];
		const auto& newInputAttachments = newSubpass.inputAttachments;
		auto& oldInputAttachments = oldSubpass.inputAttachments;
		const auto& newOutputAttachments = newSubpass.outputAttachments;
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
			auto imageView = vulkanAPI->imageViewPool.get(newInputAttachment);
			auto image = vulkanAPI->imagePool.get(imageView->getImage());
			GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
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

			auto newImageView = vulkanAPI->imageViewPool.get(newOutputAttachment);
			#if GARDEN_DEBUG
			auto oldImageView = vulkanAPI->imageViewPool.get(oldOutputAttachment.imageView);
			GARDEN_ASSERT(newImageView->getFormat() == oldImageView->getFormat());
			auto newImage = vulkanAPI->imagePool.get(newImageView->getImage());
			GARDEN_ASSERT(size == (uint2)newImage->getSize());
			#endif

			if (isFormatColor(newImageView->getFormat()))
			{
				GARDEN_ASSERT(hasAnyFlag(newImage->getBind(), Image::Bind::ColorAttachment));
				auto index = colorAttachmentIndex++;
				colorAttachments[index].imageView = newOutputAttachment;
			}
			else
			{
				GARDEN_ASSERT(hasAnyFlag(newImage->getBind(), Image::Bind::DepthStencilAttachment));
				GARDEN_ASSERT(!depthStencilAttachment.imageView);
				depthStencilAttachment.imageView = newOutputAttachment;
			}

			imageViews[imageViewIndex++] = (VkImageView)ResourceExt::getInstance(**newImageView);
			attachments.emplace(newOutputAttachment);
		}
	}

	vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer, instance);

	vk::FramebufferCreateInfo framebufferInfo({}, (VkRenderPass)renderPass, imageViews, size.x, size.y, 1);
	auto framebuffer = vulkanAPI->device.createFramebuffer(framebufferInfo);
	instance = (VkFramebuffer)framebuffer;
}

//**********************************************************************************************************************
Framebuffer::Framebuffer(uint2 size, vector<Subpass>&& subpasses)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!subpasses.empty());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		vector<vk::ImageView> imageViews;
		auto renderPass = createVkRenderPass(vulkanAPI, size, subpasses, 
			colorAttachments, depthStencilAttachment, imageViews);
		vulkanAPI->renderPasses.emplace(renderPass, 0);
		this->renderPass = (VkRenderPass)renderPass;

		vk::FramebufferCreateInfo framebufferInfo({}, renderPass, imageViews, size.x, size.y, 1);
		auto framebuffer = vulkanAPI->device.createFramebuffer(framebufferInfo);
		this->instance = (VkFramebuffer)framebuffer;
	}
	else abort();

	this->subpasses = std::move(subpasses);
	this->size = size;
	this->isSwapchain = false;
}

Framebuffer::Framebuffer(uint2 size, vector<OutputAttachment>&& colorAttachments,
	OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(size > 0u);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		if (!VulkanAPI::get()->hasDynamicRendering) // TODO: handle this case and use subpass framebuffer.
			throw GardenError("Dynamic rendering is not supported on this GPU.");
	}
	else abort();

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

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkFramebuffer(instance, renderPass);
	else abort();

	instance = nullptr;
	return true;
}

//**********************************************************************************************************************
void Framebuffer::update(uint2 size, const OutputAttachment* colorAttachments,
	uint32 colorAttachmentCount, OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(subpasses.empty());
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(colorAttachmentCount > 0 || depthStencilAttachment.imageView);

	#if GARDEN_DEBUG
	auto graphicsAPI = GraphicsAPI::get();

	for	(uint32 i = 0; i < colorAttachmentCount; i ++)
	{
		const auto& colorAttachment = colorAttachments[i];
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}

	if (depthStencilAttachment.imageView)
	{
		auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
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
void Framebuffer::update(uint2 size, vector<OutputAttachment>&& colorAttachments,
	OutputAttachment depthStencilAttachment)
{
	GARDEN_ASSERT(subpasses.empty());
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(!colorAttachments.empty() || depthStencilAttachment.imageView);

	#if GARDEN_DEBUG
	auto graphicsAPI = GraphicsAPI::get();

	for	(const auto& colorAttachment : colorAttachments)
	{
		GARDEN_ASSERT(colorAttachment.imageView);
		auto imageView = graphicsAPI->imageViewPool.get(colorAttachment.imageView);
		GARDEN_ASSERT(isFormatColor(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::ColorAttachment));
	}

	if (depthStencilAttachment.imageView)
	{
		auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
		GARDEN_ASSERT(isFormatDepthOrStencil(imageView->getFormat()));
		auto image = graphicsAPI->imagePool.get(imageView->getImage());
		GARDEN_ASSERT(size == calcSizeAtMip((uint2)image->getSize(), imageView->getBaseMip()));
		GARDEN_ASSERT(hasAnyFlag(image->getBind(), Image::Bind::DepthStencilAttachment));
	}
	#endif

	this->colorAttachments = std::move(colorAttachments);
	this->depthStencilAttachment = depthStencilAttachment;
	this->size = size;
}

//**********************************************************************************************************************
void Framebuffer::recreate(uint2 size, const vector<SubpassImages>& subpasses)
{
	GARDEN_ASSERT(size > 0u);
	GARDEN_ASSERT(subpasses.size() == this->subpasses.size());

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		recreateVkFramebuffer(size, subpasses, this->subpasses, 
			colorAttachments, depthStencilAttachment, renderPass, instance);
	}
	else abort();

	this->size = size;
}

#if GARDEN_DEBUG
//**********************************************************************************************************************
void Framebuffer::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance || subpasses.empty())
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eFramebuffer, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
		nameInfo.objectType = vk::ObjectType::eRenderPass;
		nameInfo.objectHandle = (uint64)renderPass;
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo, vulkanAPI->dynamicLoader);
	}
	else abort();
}
#endif

//**********************************************************************************************************************
void Framebuffer::beginRenderPass(const float4* clearColors, uint8 clearColorCount,
	float clearDepth, uint32 clearStencil, const int4& region, bool asyncRecording)
{
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(clearColorCount == colorAttachments.size());
	GARDEN_ASSERT(!clearColors || (clearColors && clearColorCount > 0));
	GARDEN_ASSERT(region.x + region.z <= size.x);
	GARDEN_ASSERT(region.y + region.w <= size.y);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	
	auto graphicsAPI = GraphicsAPI::get();
	graphicsAPI->currentFramebuffer = graphicsAPI->framebufferPool.getID(this);
	graphicsAPI->currentSubpassIndex = 0;

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
				subpasses.empty() ? nullptr : (VkFramebuffer)instance, 
				(VkRenderPass)renderPass, graphicsAPI->currentSubpassIndex, 
				colorAttachments, depthStencilAttachment, name);
		}
		else abort();

		for (uint32 i = 0; i < graphicsAPI->threadCount; i++)
		{
			graphicsAPI->currentPipelines[i] = {}; graphicsAPI->currentPipelineTypes[i] = {};
			graphicsAPI->currentVertexBuffers[i] = graphicsAPI->currentIndexBuffers[i] = {};
		}
	}

	BeginRenderPassCommand command;
	command.clearColorCount = clearColorCount;
	command.asyncRecording = asyncRecording;
	command.framebuffer = graphicsAPI->framebufferPool.getID(this);
	command.clearDepth = clearDepth;
	command.clearStencil = clearStencil;
	command.region = region == 0 ? int4(0, 0, size.x, size.y) : region;
	command.clearColors = clearColors;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		readyLock++;
		graphicsAPI->currentCommandBuffer->addLockResource(command.framebuffer);
	}

	graphicsAPI->isCurrentRenderPassAsync = asyncRecording;
}

//**********************************************************************************************************************
void Framebuffer::nextSubpass(bool asyncRecording)
{
	GARDEN_ASSERT(GraphicsAPI::get()->currentFramebuffer == GraphicsAPI::get()->framebufferPool.getID(this));
	GARDEN_ASSERT(GraphicsAPI::get()->currentSubpassIndex + 1 < subpasses.size());
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	auto graphicsAPI = GraphicsAPI::get();
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto swapchain = vulkanAPI->vulkanSwapchain;

		if (!vulkanAPI->secondaryCommandBuffers.empty())
			swapchain->endSecondaryCommandBuffers();

		if (asyncRecording)
		{
			#if GARDEN_DEBUG
			const auto& name = debugName;
			#else
			string name;
			#endif

			swapchain->beginSecondaryCommandBuffers(
				subpasses.empty() ? nullptr : (VkFramebuffer)instance, 
				(VkRenderPass)renderPass, graphicsAPI->currentSubpassIndex + 1, 
				colorAttachments, depthStencilAttachment, name);
		}
	}
	else abort();
		
	if (asyncRecording)
	{
		for (uint32 i = 0; i < graphicsAPI->threadCount; i++)
			graphicsAPI->currentVertexBuffers[i] = graphicsAPI->currentIndexBuffers[i] = {};
	}
	
	NextSubpassCommand command;
	command.asyncRecording = asyncRecording;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	graphicsAPI->isCurrentRenderPassAsync = asyncRecording;
	graphicsAPI->currentSubpassIndex++;
}
void Framebuffer::endRenderPass()
{
	GARDEN_ASSERT(GraphicsAPI::get()->currentFramebuffer == GraphicsAPI::get()->framebufferPool.getID(this));
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);

	auto graphicsAPI = GraphicsAPI::get();
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
	graphicsAPI->currentSubpassIndex = 0;
	graphicsAPI->currentFramebuffer = {};
}

//**********************************************************************************************************************
void Framebuffer::clearAttachments(const ClearAttachment* attachments,
	uint8 attachmentCount, const ClearRegion* regions, uint32 regionCount)
{
	GARDEN_ASSERT(GraphicsAPI::get()->currentFramebuffer == GraphicsAPI::get()->framebufferPool.getID(this));
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	ClearAttachmentsCommand command;
	command.attachmentCount = attachmentCount;
	command.regionCount = regionCount;
	command.framebuffer = graphicsAPI->framebufferPool.getID(this);
	command.attachments = attachments;
	command.regions = regions;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}