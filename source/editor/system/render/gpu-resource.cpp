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

#include "garden/editor/system/render/gpu-resource.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/vulkan/api.hpp"
#include "garden/file.hpp"

using namespace garden;

// TODO: Read back buffer and images data. But we need somehow detect which queue uses it to prevent sync problems.
//       maybe iterate command buffers and detect where it used? Also we can iterate command buffers to detect invalid sync.

//**********************************************************************************************************************
GpuResourceEditorSystem::GpuResourceEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", GpuResourceEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", GpuResourceEditorSystem::deinit);
}
GpuResourceEditorSystem::~GpuResourceEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", GpuResourceEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", GpuResourceEditorSystem::deinit);
	}
}

void GpuResourceEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", GpuResourceEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", GpuResourceEditorSystem::editorBarTool);
}
void GpuResourceEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", GpuResourceEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", GpuResourceEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderItemList(uint32 count, uint32 occupancy, uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, Resource* items, psize itemSize, string& debugName, const char* resourceName)
{
	ImGui::TextWrapped("%lu/%lu (count/occupancy)", (unsigned long)count, (unsigned long)occupancy);
	ImGui::BeginChild("##itemList", ImVec2(256.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)),
		ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);
	
	for (uint32 i = 0; i < occupancy; i++)
	{
		auto item = (Resource*)((uint8*)items + i * itemSize);
		if (!ResourceExt::getInstance(*item))
			continue;
		
		auto name = item->getDebugName().empty() ? 
			resourceName + to_string(i + 1) : item->getDebugName();
		if (!searchString.empty())
		{
			if (!find(name, searchString, i + 1, searchCaseSensitive))
				continue;
		}

		ImGui::PushID(to_string(i + 1).c_str());
		if (ImGui::Selectable(name.c_str(), selectedItem == i))
			selectedItem = i;
		ImGui::PopID();

		if (selectedItem == i)
			debugName = name;

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Copy Name"))
				ImGui::SetClipboardText(name.c_str());
			ImGui::EndPopup();
		}
	}

	ImGui::PopStyleColor();
	ImGui::EndChild();
	ImGui::SameLine();
}
static void renderSearch(string& searchString, bool& searchCaseSensitive)
{
	ImGui::Spacing();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Search", &searchString); ImGui::SameLine();
	ImGui::Checkbox("Aa", &searchCaseSensitive); ImGui::SameLine();
}

//**********************************************************************************************************************
static void getVkAllocationInfo(VmaAllocationInfo& allocationInfo, 
	vk::MemoryType& memoryType, vk::MemoryHeap& memoryHeap, Memory& memory)
{
	auto allocation = (VmaAllocation)MemoryExt::getAllocation(memory);
	if (allocation)
	{
		auto vulkanAPI = VulkanAPI::get();
		vmaGetAllocationInfo(vulkanAPI->memoryAllocator, allocation, &allocationInfo);
		auto memoryProperties = vulkanAPI->physicalDevice.getMemoryProperties2();
		memoryType = memoryProperties.memoryProperties.memoryTypes[allocationInfo.memoryType];
		memoryHeap = memoryProperties.memoryProperties.memoryHeaps[memoryType.heapIndex];
	}
}
static void renderVkMemoryDetails(const VmaAllocationInfo& allocationInfo, 
	const vk::MemoryType& memoryType, const vk::MemoryHeap& memoryHeap, const Memory& memory)
{
	ImGui::TextWrapped("Binary size: %s", toBinarySizeString(memory.getBinarySize()).c_str());
	ImGui::TextWrapped("Memory CPU access: %s", toString(memory.getCpuAccess()).data());
	ImGui::TextWrapped("Memory location: %s", toString(memory.getLocation()).data());
	ImGui::TextWrapped("Memory strategy: %s", toString(memory.getStrategy()).data());
	ImGui::TextWrapped("Allocation size: %s", toBinarySizeString(allocationInfo.size).c_str());
	ImGui::TextWrapped("Allocation offset: %s", toBinarySizeString(allocationInfo.offset).c_str());
	ImGui::TextWrapped("Memory type index: %lu", (long unsigned)allocationInfo.memoryType);
	ImGui::TextWrapped("Memory heap index: %lu", (long unsigned)memoryType.heapIndex);
	ImGui::TextWrapped("Memory type: %s", vk::to_string(memoryType.propertyFlags).c_str());
	ImGui::TextWrapped("Memory heap: %s", vk::to_string(memoryHeap.flags).c_str());
	ImGui::TextWrapped("Memory heap size: %s", toBinarySizeString(memoryHeap.size).c_str());
}

//**********************************************************************************************************************
static void renderBuffers(uint32& selectedItem, string& searchString, bool& searchCaseSensitive)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto buffers = graphicsAPI->bufferPool.getData();

	string bufferName;
	renderItemList(graphicsAPI->bufferPool.getCount(), graphicsAPI->bufferPool.getOccupancy(),
		selectedItem, searchString, searchCaseSensitive, buffers, sizeof(Buffer), bufferName, "Buffer ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->bufferPool.getCount() == 0 || selectedItem >= graphicsAPI->bufferPool.getOccupancy() ||
		!ResourceExt::getInstance(buffers[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& buffer = buffers[selectedItem];
	ImGui::SeparatorText(bufferName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(buffer));
	ImGui::TextWrapped("Usage: { %s }", toStringList(buffer.getUsage()).c_str());

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		VmaAllocationInfo allocationInfo = {};
		vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
		getVkAllocationInfo(allocationInfo, memoryType, memoryHeap, buffer);
		renderVkMemoryDetails(allocationInfo, memoryType, memoryHeap, buffer);
	}

	auto isMappable = buffer.isMappable();
	ImGui::Checkbox("Mappable", &isMappable);
	ImGui::Spacing();

	// TODO: using descriptor sets.

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImages(uint32& selectedItem, string& searchString, bool& searchCaseSensitive, 
	GpuResourceEditorSystem::TabType& openNextTab, int& imageMip, int& imageLayer)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto images = graphicsAPI->imagePool.getData();

	string imageName;
	renderItemList(graphicsAPI->imagePool.getCount(), graphicsAPI->imagePool.getOccupancy(),
		selectedItem, searchString, searchCaseSensitive, images, sizeof(Image), imageName, "Image ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->imagePool.getCount() == 0 || selectedItem >= graphicsAPI->imagePool.getOccupancy() ||
		!ResourceExt::getInstance(images[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& image = images[selectedItem];
	auto size = image.getSize();

	if (hasAnyFlag(image.getUsage(), Image::Usage::Sampled) && image.isReady())
	{
		imageMip = std::min(imageMip, (int)image.getMipCount() - 1);
		imageLayer = std::min(imageLayer, (int)image.getSize().getZ() - 1);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto imageView = graphicsSystem->createImageView(
			graphicsAPI->imagePool.getID(&image), Image::Type::Texture2D,
			Image::Format::Undefined, imageMip, 1, imageLayer, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.editor.gpuResource.tmp");
		graphicsSystem->destroy(imageView); // TODO: suboptimal solution

		auto aspectRatio = (float)size.getY() / size.getX();
		ImGui::Image(*imageView, ImVec2(256.0f, 256.0f * aspectRatio));

		if (image.getMipCount() > 1)
			ImGui::SliderInt("Mip", &imageMip, 0, image.getMipCount() - 1);
		if ((int)image.getSize().getZ() > 1)
			ImGui::SliderInt("Layer", &imageLayer, 0, (int)image.getSize().getZ() - 1);
	}

	ImGui::SeparatorText(imageName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(image));
	ImGui::TextWrapped("Image type: %s", toString(image.getType()).data());
	ImGui::TextWrapped("Format type: %s", toString(image.getFormat()).data());
	ImGui::TextWrapped("Usage: { %s }", toStringList(image.getUsage()).c_str());
	ImGui::TextWrapped("Image size: %lux%lux%lu", (unsigned long)size.getX(), 
		(unsigned long)size.getY(), (unsigned long)size.getZ());
	ImGui::TextWrapped("Mip count: %lu", (unsigned long)image.getMipCount());

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		VmaAllocationInfo allocationInfo = {};
		vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
		getVkAllocationInfo(allocationInfo, memoryType, memoryHeap, image);
		renderVkMemoryDetails(allocationInfo, memoryType, memoryHeap, image);
	}
	else abort();
	
	auto isSwapchain = image.isSwapchain();
	ImGui::Checkbox("Swapchain", &isSwapchain);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Image Views"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		const auto imageViews = graphicsAPI->imageViewPool.getData();
		auto occupancy = graphicsAPI->imageViewPool.getOccupancy();
		auto selectedImage = graphicsAPI->imagePool.getID(&image);

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			const auto& imageView = imageViews[i];
			if (imageView.getImage() != selectedImage)
				continue;
			
			auto imageViewName = imageView.getDebugName().empty() ? "Image View " +
				to_string(i + 1) : imageView.getDebugName();
			isAny = true;

			ImGui::PushID(to_string(i + 1).c_str());
			if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
					selectedItem = i;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (!isAny)
		{
			ImGui::Indent();
			ImGui::TextDisabled("None");
			ImGui::Unindent();
		}

		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImageViews(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	const auto imageViews = graphicsAPI->imageViewPool.getData();

	string imageViewName;
	renderItemList(graphicsAPI->imageViewPool.getCount(), graphicsAPI->imageViewPool.getOccupancy(),
		selectedItem, searchString, searchCaseSensitive, imageViews, sizeof(ImageView), imageViewName, "Image View ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->imageViewPool.getCount() == 0 || selectedItem >= graphicsAPI->imageViewPool.getOccupancy() ||
		!ResourceExt::getInstance(imageViews[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& imageView = imageViews[selectedItem];
	auto isDefault = imageView.isDefault();

	string imageName;
	if (imageView.getImage())
	{
		auto image = graphicsAPI->imagePool.get(imageView.getImage());
		imageName = image->getDebugName().empty() ? "Image " +
			to_string(*imageView.getImage()) : image->getDebugName();

		if (hasAnyFlag(image->getUsage(), Image::Usage::Sampled) && image->isReady())
		{
			auto size = image->getSize();
			auto aspectRatio = (float)size.getY() / size.getX();
			if (imageView.getType() == Image::Type::Texture2D)
				ImGui::Image(selectedItem + 1, ImVec2(256.0f, 256.0f * aspectRatio));
		}
	}

	ImGui::SeparatorText(imageViewName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(imageView));
	ImGui::TextWrapped("Image: %s", imageName.c_str());
	ImGui::TextWrapped("Image type: %s", toString(imageView.getType()).data());
	ImGui::TextWrapped("Format type: %s", toString(imageView.getFormat()).data());
	ImGui::TextWrapped("Base layer: %lu", (unsigned long)imageView.getBaseLayer());
	ImGui::TextWrapped("Layer count: %lu", (unsigned long)imageView.getLayerCount());
	ImGui::TextWrapped("Base mip: %lu", (unsigned long)imageView.getBaseMip());
	ImGui::TextWrapped("Mip count: %lu", (unsigned long)imageView.getMipCount());
	ImGui::Checkbox("Default", &isDefault);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Framebuffers"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		const auto framebuffers = graphicsAPI->framebufferPool.getData();
		auto occupancy = graphicsAPI->framebufferPool.getOccupancy();
		auto selectedImageView = graphicsAPI->imageViewPool.getID(&imageView);

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			const auto& framebuffer = framebuffers[i];
			const auto& colorAttachments = framebuffer.getColorAttachments();

			auto isFound = false;
			for (auto& colorAttachment : colorAttachments)
			{
				if (colorAttachment.imageView == selectedImageView)
				{
					isFound = true;
					break;
				}
			}

			const auto& depthStencilAttachment = framebuffer.getDepthStencilAttachment();
			if (depthStencilAttachment.imageView == selectedImageView)
				isFound = true;

			if (!isFound)
				continue;

			auto framebufferName = framebuffer.getDebugName().empty() ? "Framebuffer " +
				to_string(i + 1) : framebuffer.getDebugName();
			isAny = true;

			ImGui::PushID(to_string(i + 1).c_str());
			if (ImGui::TreeNodeEx(framebufferName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::Framebuffers;
					selectedItem = i;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (!isAny)
		{
			ImGui::Indent();
			ImGui::TextDisabled("None");
			ImGui::Unindent();
		}

		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	// TODO: using descriptor sets

	ImGui::Spacing();

	if (ImGui::Button("Select parent image", ImVec2(-FLT_MIN, 0.0f)))
	{
		openNextTab = GpuResourceEditorSystem::TabType::Images;
		selectedItem = *imageView.getImage() - 1;
	}

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderFramebuffers(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto framebuffers = graphicsAPI->framebufferPool.getData();

	string framebufferName;
	renderItemList(graphicsAPI->framebufferPool.getCount(), graphicsAPI->framebufferPool.getOccupancy(), selectedItem,
		searchString, searchCaseSensitive, framebuffers, sizeof(Framebuffer), framebufferName, "Framebuffer ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->framebufferPool.getCount() == 0 || selectedItem >= graphicsAPI->framebufferPool.getOccupancy() ||
		!ResourceExt::getInstance(framebuffers[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& framebuffer = framebuffers[selectedItem];
	ImGui::SeparatorText(framebufferName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(framebuffer));
	ImGui::TextWrapped("Size: %lux%lu", (unsigned long)framebuffer.getSize().x, (unsigned long)framebuffer.getSize().y);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Color attachments"))
	{
		ImGui::Indent();

		const auto& colorAttachments = framebuffer.getColorAttachments().data();
		auto colorAttachmentCount = (uint32)framebuffer.getColorAttachments().size();
		
		for (uint32 i = 0; i < colorAttachmentCount; i++)
		{
			auto attachment = colorAttachments[i];
			if (!attachment.imageView)
			{
				ImGui::Text("[unused]");
				continue;
			}
			
			auto imageView = graphicsAPI->imageViewPool.get(attachment.imageView);
			auto viewName = imageView->getDebugName().empty() ? "Image View " +
				to_string(*attachment.imageView) : imageView->getDebugName();
			ImGui::SeparatorText(to_string(i).c_str());
			ImGui::Text("%s", viewName.c_str());
			ImGui::PushID(viewName.c_str());

			auto value = attachment.flags.clear;
			ImGui::Checkbox("Clear", &value); ImGui::SameLine();
			value = attachment.flags.load;
			ImGui::Checkbox("Load", &value); ImGui::SameLine();
			value = attachment.flags.store;
			ImGui::Checkbox("Store", &value); ImGui::SameLine();
			
			if (ImGui::Button("Select image view")) 
			{
				openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
				selectedItem = *attachment.imageView - 1;
			}
			ImGui::PopID();
		}

		if (framebuffer.getColorAttachments().empty())
			ImGui::TextDisabled("None");
		
		ImGui::Unindent();
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Depth/Stencil attachment"))
	{
		ImGui::Indent();

		auto depthStencilAttachment = framebuffer.getDepthStencilAttachment();
		if (depthStencilAttachment.imageView)
		{
			auto imageView = graphicsAPI->imageViewPool.get(depthStencilAttachment.imageView);
			auto viewName = imageView->getDebugName().empty() ? "Image View " +
				to_string(*depthStencilAttachment.imageView) : imageView->getDebugName();
			ImGui::Text("%s", viewName.c_str());
			ImGui::PushID(viewName.c_str());

			auto value = depthStencilAttachment.flags.clear;
			ImGui::Checkbox("Clear", &value); ImGui::SameLine();
			value = depthStencilAttachment.flags.load;
			ImGui::Checkbox("Load", &value); ImGui::SameLine();
			value = depthStencilAttachment.flags.store;
			ImGui::Checkbox("Store", &value); ImGui::SameLine();

			if (ImGui::Button("Select image view"))
			{
				openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
				selectedItem = *depthStencilAttachment.imageView - 1;
			}
			ImGui::PopID();
		}
		else
		{
			ImGui::TextDisabled("None");
		}

		ImGui::Unindent();
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Subpasses"))
	{
		ImGui::Indent();

		const auto& subpasses = framebuffer.getSubpasses().data();
		auto subpassCount = (uint32)framebuffer.getSubpasses().size();

		for (uint32 i = 0; i < subpassCount; i++)
		{
			const auto& subpass = subpasses[i];
			ImGui::SeparatorText(to_string(i).c_str());
			ImGui::TextWrapped("Pipeline type: %s", toString(subpass.pipelineType).data());
			ImGui::TextWrapped("Input attachment count: %lu", (unsigned long)subpass.inputAttachments.size());
			ImGui::TextWrapped("Output attachment count: %lu", (unsigned long)subpass.outputAttachments.size());
			// TODO: render input and output attachment list.
		}

		if (framebuffer.getSubpasses().empty())
			ImGui::TextDisabled("None");
		
		ImGui::Unindent();
		ImGui::Spacing();
	}

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderSamplers(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto samplers = graphicsAPI->samplerPool.getData();

	string descriptorSetName;
	renderItemList(graphicsAPI->samplerPool.getCount(), graphicsAPI->samplerPool.getOccupancy(), selectedItem,
		searchString, searchCaseSensitive, samplers, sizeof(Sampler), descriptorSetName, "Sampler ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->samplerPool.getCount() == 0 ||
		selectedItem >= graphicsAPI->samplerPool.getOccupancy() ||
		!ResourceExt::getInstance(samplers[selectedItem]))
	{
		ImGui::TextWrapped("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& sampler = samplers[selectedItem];
	auto state = sampler.getState();
	ImGui::SeparatorText(descriptorSetName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(sampler));
	ImGui::TextWrapped("Minification Filter: %s", toString(state.minFilter).data());
	ImGui::TextWrapped("Magnification Filter: %s", toString(state.magFilter).data());
	ImGui::TextWrapped("Mipmap Filter: %s", toString(state.mipmapFilter).data());
	ImGui::TextWrapped("Address Mode X: %s", toString(state.addressModeX).data());
	ImGui::TextWrapped("Address Mode Y: %s", toString(state.addressModeY).data());
	ImGui::TextWrapped("Address Mode Z: %s", toString(state.addressModeZ).data());
	ImGui::TextWrapped("Border Color: %s", toString(state.borderColor).data());
	ImGui::TextWrapped("Compare Operation: %s", toString(state.compareOperation).data());
	ImGui::TextWrapped("Maximum Anisotropy: %f", state.maxAnisotropy);
	ImGui::TextWrapped("Mip LOD Bias: %f", state.mipLodBias);
	ImGui::TextWrapped("Minimum LOD: %f", state.minLod);
	ImGui::TextWrapped("Maximum LOD: %f", state.maxLod);
	auto boolValue = (bool)state.anisoFiltering;
	ImGui::Checkbox("Anisotropic Filtering", &boolValue);
	boolValue = (bool)state.comparison;
	ImGui::Checkbox("Comparison", &boolValue);
	boolValue = (bool)state.unnormCoords;
	ImGui::Checkbox("Unnormalized Coordinates", &boolValue);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Descriptor Sets"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		const auto descriptorSets = graphicsAPI->descriptorSetPool.getData();
		auto occupancy = graphicsAPI->descriptorSetPool.getOccupancy();
		auto selectedSampler = graphicsAPI->samplerPool.getID(&sampler);

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			const auto& descriptorSet = descriptorSets[i];
			const auto& dsSamplers = descriptorSet.getSamplers();

			auto isFound = false;
			for (const auto& dsSampler : dsSamplers)
			{
				if (dsSampler.second == selectedSampler)
				{
					isFound = true;
					break;
				}
			}

			if (!isFound)
				continue;

			auto descriptorSetName = descriptorSet.getDebugName().empty() ? 
				"Descriptor Set " + to_string(i + 1) : descriptorSet.getDebugName();
			isAny = true;

			ImGui::PushID(to_string(i + 1).c_str());
			if (ImGui::TreeNodeEx(descriptorSetName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::DescriptorSets;
					selectedItem = i;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (!isAny)
		{
			ImGui::Indent();
			ImGui::TextDisabled("None");
			ImGui::Unindent();
		}

		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderDescriptorSets(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto descriptorSets = graphicsAPI->descriptorSetPool.getData();

	string descriptorSetName;
	renderItemList(graphicsAPI->descriptorSetPool.getCount(), graphicsAPI->descriptorSetPool.getOccupancy(), selectedItem,
		searchString, searchCaseSensitive, descriptorSets, sizeof(DescriptorSet), descriptorSetName, "Descriptor Set ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->descriptorSetPool.getCount() == 0 ||
		selectedItem >= graphicsAPI->descriptorSetPool.getOccupancy() ||
		!ResourceExt::getInstance(descriptorSets[selectedItem]))
	{
		ImGui::TextWrapped("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& descriptorSet = descriptorSets[selectedItem];
	const Pipeline::Uniforms* uniforms = nullptr; string pipelineName;
	if (descriptorSet.getPipeline())
	{
		auto pipelineView = graphicsAPI->getPipelineView(
			descriptorSet.getPipelineType(), descriptorSet.getPipeline());
		pipelineName = pipelineView->getDebugName().empty() ? "Pipeline " +
			to_string(*descriptorSet.getPipeline()) : pipelineView->getDebugName();
		uniforms = &pipelineView->getUniforms();
	}

	ImGui::SeparatorText(descriptorSetName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(descriptorSet));
	ImGui::TextWrapped("Pipeline: %s", pipelineName.c_str());
	ImGui::TextWrapped("Pipeline type: %s", toString(descriptorSet.getPipelineType()).data());
	ImGui::TextWrapped("Index: %lu", (unsigned long)descriptorSet.getIndex());
	ImGui::TextWrapped("Set count: %lu", (unsigned long)descriptorSet.getSetCount());
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Uniforms"))
	{
		ImGui::Indent();

		const auto& descriptorUniforms = descriptorSet.getUniforms();
		for (const auto& pair : descriptorUniforms)
		{
			const auto uniform = uniforms->find(pair.first);
			if (uniform == uniforms->end() ||
				uniform->second.descriptorSetIndex != descriptorSet.getIndex())
			{
				continue;
			}

			ImGui::SeparatorText(pair.first.c_str());
			ImGui::PushID(pair.first.c_str());

			const auto& resourceSets = pair.second.resourceSets.data();
			auto resourceSetCount = (uint32)pair.second.resourceSets.size();

			auto type = uniform->second.type;
			if (isBufferType(type))
			{
				for (uint32 i = 0; i < resourceSetCount; i++)
				{
					if (ImGui::TreeNodeEx(to_string(i).c_str()))
					{
						const auto& resourceArray = resourceSets[i];
						for (auto resource : resourceArray)
						{
							auto bufferView = graphicsAPI->bufferPool.get(ID<Buffer>(resource));
							auto imageViewName = bufferView->getDebugName().empty() ? "Buffer " +
								to_string(*resource) : bufferView->getDebugName();
							ImGui::PushID(to_string(i + 1).c_str());
							if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
							{
								if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								{
									openNextTab = GpuResourceEditorSystem::TabType::Buffers;
									selectedItem = *resource - 1;
									ImGui::TreePop();
									ImGui::PopID();
									break;
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
						}
						ImGui::TreePop();
					}
				}
			}
			else if (isSamplerType(type) || isImageType(type))
			{
				for (uint32 i = 0; i < resourceSetCount; i++)
				{
					if (ImGui::TreeNodeEx(to_string(i).c_str()))
					{
						const auto& resourceArray = resourceSets[i];
						for (auto resource : resourceArray)
						{
							auto imageView = graphicsAPI->imageViewPool.get(ID<ImageView>(resource));
							auto imageViewName = imageView->getDebugName().empty() ? "Image View " +
								to_string(*resource) : imageView->getDebugName();
							ImGui::PushID(to_string(*resource).c_str());
							if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
							{
								if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								{
									openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
									selectedItem = *resource - 1;
									ImGui::TreePop();
									ImGui::PopID();
									break;
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
						}
						ImGui::TreePop();
					}
				}
			}

			ImGui::PopID();
		}

		if (descriptorUniforms.empty())
			ImGui::TextDisabled("None");

		ImGui::Unindent();
		ImGui::Spacing();
	}

	ImGui::Spacing();

	if (ImGui::Button("Select parent pipeline", ImVec2(-FLT_MIN, 0.0f)))
	{
		if (descriptorSet.getPipelineType() == PipelineType::Graphics)
			openNextTab = GpuResourceEditorSystem::TabType::GraphicsPipelines;
		else
			openNextTab = GpuResourceEditorSystem::TabType::ComputePipelines;
		selectedItem = *descriptorSet.getPipeline() - 1;
	}

	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderPipelineDetails(const Pipeline& pipeline, ID<Pipeline> instance,
	GpuResourceEditorSystem::TabType& openNextTab, uint32& selectedItem)
{
	auto useAsyncRecording = pipeline.useAsyncRecording();
	auto isBindless = pipeline.isBindless();
	ImGui::TextWrapped("Path: %s", pipeline.getPath().generic_string().c_str());
	ImGui::TextWrapped("Variant count: %lu", (unsigned long)pipeline.getVariantCount());
	ImGui::TextWrapped("Push constants size: %s", toBinarySizeString(pipeline.getPushConstantsSize()).c_str());
	ImGui::TextWrapped("Max bindless count: %lu", (unsigned long)pipeline.getMaxBindlessCount());
	ImGui::Checkbox("Async Recording", &useAsyncRecording); ImGui::SameLine();
	ImGui::Checkbox("Bindless", &isBindless);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Uniforms"))
	{
		ImGui::Indent();

		const auto& uniforms = pipeline.getUniforms();
		for (const auto& pair : uniforms)
		{
			auto uniform = pair.second;
			auto readAccess = uniform.readAccess;
			auto writeAccess = uniform.writeAccess;
			auto isMutable = uniform.isMutable;

			ImGui::SeparatorText(pair.first.c_str());
			ImGui::TextWrapped("Type: %s", toString(uniform.type).data());
			ImGui::TextWrapped("Shader stages: %s", toStringList(uniform.shaderStages).data());
			ImGui::TextWrapped("Binding index: %lu", (unsigned long)uniform.bindingIndex);
			ImGui::TextWrapped("Descriptor set index: %lu", (unsigned long)uniform.descriptorSetIndex);
			ImGui::TextWrapped("Array size: %lu", (unsigned long)uniform.arraySize);
			
			ImGui::PushID(pair.first.c_str());
			ImGui::Text("Access:"); ImGui::SameLine();
			ImGui::Checkbox("Read", &readAccess); ImGui::SameLine();
			ImGui::Checkbox("Write", &writeAccess);
			ImGui::Checkbox("Mutable", &isMutable);
			ImGui::PopID();
		}

		if (uniforms.empty())
			ImGui::TextDisabled("None");

		ImGui::Unindent();
		ImGui::Spacing();
	}

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Descriptor Sets"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto graphicsAPI = GraphicsAPI::get();
		const auto descriptorSets = graphicsAPI->descriptorSetPool.getData();
		auto occupancy = graphicsAPI->descriptorSetPool.getOccupancy();

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			const auto& descriptorSet = descriptorSets[i];
			if (descriptorSet.getPipelineType() != pipeline.getType() ||
				descriptorSet.getPipeline() != instance)
			{
				continue;
			}

			auto descriptorSetName = descriptorSet.getDebugName().empty() ? "Descriptor Set " +
				to_string(i + 1) : descriptorSet.getDebugName();
			isAny = true;

			ImGui::PushID(to_string(i + 1).c_str());
			if (ImGui::TreeNodeEx(descriptorSetName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::DescriptorSets;
					selectedItem = i;
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (!isAny)
		{
			ImGui::Indent();
			ImGui::TextDisabled("None");
			ImGui::Unindent();
		}

		ImGui::PopStyleColor();
		ImGui::Spacing();
	}
}

//**********************************************************************************************************************
static void renderGraphicsPipelines(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto graphicsPipelines = graphicsAPI->graphicsPipelinePool.getData();

	string graphicsPipelineName;
	renderItemList(graphicsAPI->graphicsPipelinePool.getCount(), graphicsAPI->graphicsPipelinePool.getOccupancy(),
		selectedItem, searchString, searchCaseSensitive, graphicsPipelines,
		sizeof(GraphicsPipeline), graphicsPipelineName, "Graphics Pipeline ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->graphicsPipelinePool.getCount() == 0 ||
		selectedItem >= graphicsAPI->graphicsPipelinePool.getOccupancy() ||
		!ResourceExt::getInstance(graphicsPipelines[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& graphicsPipeline = graphicsPipelines[selectedItem];
	string framebufferName;
	if (graphicsPipeline.getFramebuffer())
	{
		auto framebuffer = graphicsAPI->framebufferPool.get(graphicsPipeline.getFramebuffer());
		framebufferName = framebuffer->getDebugName().empty() ? "Framebuffer " +
			to_string(*graphicsPipeline.getFramebuffer()) : framebuffer->getDebugName();
	}

	ImGui::SeparatorText(graphicsPipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(graphicsPipeline));
	ImGui::TextWrapped("Framebuffer: %s", framebufferName.c_str());
	ImGui::TextWrapped("Subpass index: %lu", (unsigned long)graphicsPipeline.getSubpassIndex());
	auto instance = ID<Pipeline>(graphicsAPI->graphicsPipelinePool.getID(&graphicsPipeline));
	renderPipelineDetails(graphicsPipeline, instance, openNextTab, selectedItem);
	ImGui::Spacing();
	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderComputePipelines(uint32& selectedItem, string& searchString,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	auto graphicsAPI = GraphicsAPI::get();
	auto computePipelines = graphicsAPI->computePipelinePool.getData();

	string computePipelineName;
	renderItemList(graphicsAPI->computePipelinePool.getCount(), graphicsAPI->computePipelinePool.getOccupancy(),
		selectedItem, searchString, searchCaseSensitive, computePipelines,
		sizeof(ComputePipeline), computePipelineName, "Compute Pipeline ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (graphicsAPI->computePipelinePool.getCount() == 0 ||
		selectedItem >= graphicsAPI->computePipelinePool.getOccupancy() ||
		!ResourceExt::getInstance(computePipelines[selectedItem]))
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(searchString, searchCaseSensitive);
		return;
	}

	auto& computePipeline = computePipelines[selectedItem];
	auto localSize = computePipeline.getLocalSize();

	ImGui::SeparatorText(computePipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Ready lock: %lu", (unsigned long)ResourceExt::getReadyLock(computePipeline));
	ImGui::TextWrapped("Local size: %lux%lux%lu", (unsigned long)localSize.getX(), 
		(unsigned long)localSize.getY(), (unsigned long)localSize.getZ());
	auto instance = ID<Pipeline>(graphicsAPI->computePipelinePool.getID(&computePipeline));
	renderPipelineDetails(computePipeline, instance, openNextTab, selectedItem);
	ImGui::Spacing();
	ImGui::EndChild();
	renderSearch(searchString, searchCaseSensitive);
}

//**********************************************************************************************************************
void GpuResourceEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	ImGui::SetNextWindowSize(ImVec2(750.0f, 450.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("GPU Resource Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll))
		{
			auto lastTabType = openNextTab;
			if (ImGui::BeginTabItem("Buffers", nullptr, openNextTab == 
				TabType::Buffers ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderBuffers(selectedItem, searchString, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Images", nullptr, openNextTab == 
				TabType::Images ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderImages(selectedItem, searchString, searchCaseSensitive, openNextTab, imageMip, imageLayer);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Image Views", nullptr, openNextTab == 
				TabType::ImageViews ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderImageViews(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Framebuffers", nullptr, openNextTab == 
				TabType::Framebuffers ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderFramebuffers(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Samplers", nullptr, openNextTab == 
				TabType::Samplers ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderSamplers(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Descriptor Sets", nullptr, openNextTab == 
				TabType::DescriptorSets ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderDescriptorSets(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Graphics Pipelines", nullptr, openNextTab == 
				TabType::GraphicsPipelines ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderGraphicsPipelines(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Compute Pipelines", nullptr, openNextTab == 
				TabType::ComputePipelines ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderComputePipelines(selectedItem, searchString, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}

			if (lastTabType == openNextTab)
				openNextTab = TabType::None;

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

//**********************************************************************************************************************
void GpuResourceEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("GPU Resource Viewer"))
		showWindow = true;
}

void GpuResourceEditorSystem::openTab(ID<Resource> resource, TabType type) noexcept
{
	GARDEN_ASSERT(resource);
	selectedItem = *resource - 1;
	openNextTab = type;
	showWindow = true;
}
#endif