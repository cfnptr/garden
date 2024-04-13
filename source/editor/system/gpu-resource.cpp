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

#include "garden/editor/system/gpu-resource.hpp"

#if GARDEN_EDITOR
#include "garden/file.hpp"

using namespace garden;

// TODO: Read back buffer and images data. But we need somehow detect which queue uses it to prevent sync problems.
//       maybe iterate command buffers and detect where it used? Also we can iterate command buffers to detect invalid sync.

//**********************************************************************************************************************
GpuResourceEditorSystem::GpuResourceEditorSystem(Manager* manager,
	GraphicsSystem* system) : EditorSystem(manager, system)
{
	SUBSCRIBE_TO_EVENT("EditorRender", GpuResourceEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", GpuResourceEditorSystem::editorBarTool);
}
GpuResourceEditorSystem::~GpuResourceEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", GpuResourceEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", GpuResourceEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderItemList(uint32 count, uint32 occupancy, uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, Resource* items, psize itemSize, string& debugName, const char* resourceName)
{
	ImGui::TextWrapped("%lu/%lu (count/occupancy)", (unsigned long)count, (unsigned long)occupancy);
	ImGui::BeginChild("##itemList", ImVec2(256, -(ImGui::GetFrameHeightWithSpacing() + 4)),
		ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);
	
	for (uint32 i = 0; i < occupancy; i++)
	{
		auto item = (Resource*)((uint8*)items + i * itemSize);
		if (!ResourceExt::getInstance(*item))
			continue;
		
		auto name = item->getDebugName().empty() ? 
			resourceName + to_string(i + 1) : item->getDebugName();
		if (!resourceSearch.empty())
		{
			if (!find(name, resourceSearch, searchCaseSensitive))
				continue;
		}

		if (ImGui::Selectable(name.c_str(), selectedItem == i))
			selectedItem = i;
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

	if (selectedItem >= occupancy)
		selectedItem = occupancy > 0 ? occupancy - 1 : 0;
}
static void renderSearch(string& resourceSearch, bool& searchCaseSensitive)
{
	ImGui::Spacing();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Search", &resourceSearch); ImGui::SameLine();
	ImGui::Checkbox("Aa", &searchCaseSensitive); ImGui::SameLine();
}

//**********************************************************************************************************************
static void getAllocationInfo(VmaAllocationInfo& allocationInfo, 
	vk::MemoryType& memoryType, vk::MemoryHeap& memoryHeap, Memory& memory)
{
	auto allocation = (VmaAllocation)MemoryExt::getAllocation(memory);
	if (allocation)
	{
		vmaGetAllocationInfo(Vulkan::memoryAllocator, allocation, &allocationInfo);
		auto memoryProperties = Vulkan::physicalDevice.getMemoryProperties2();
		memoryType = memoryProperties.memoryProperties.memoryTypes[allocationInfo.memoryType];
		memoryHeap = memoryProperties.memoryProperties.memoryHeaps[memoryType.heapIndex];
	}
}
static void renderMemoryDetails(const VmaAllocationInfo& allocationInfo, 
	const vk::MemoryType& memoryType, const vk::MemoryHeap& memoryHeap, const Memory& memory)
{
	ImGui::TextWrapped("Binary size: %s", toBinarySizeString(memory.getBinarySize()).c_str());
	ImGui::TextWrapped("Memory access: %s", toString(memory.getMemoryAccess()).data());
	ImGui::TextWrapped("Memory usage: %s", toString(memory.getMemoryUsage()).data());
	ImGui::TextWrapped("Memory strategy: %s", toString(memory.getMemoryStrategy()).data());
	ImGui::TextWrapped("Allocation size: %s", toBinarySizeString(allocationInfo.size).c_str());
	ImGui::TextWrapped("Allocation offset: %s", toBinarySizeString(allocationInfo.offset).c_str());
	ImGui::TextWrapped("Memory type: %s", vk::to_string(memoryType.propertyFlags).c_str());
	ImGui::TextWrapped("Memory heap: %s", vk::to_string(memoryHeap.flags).c_str());
	ImGui::TextWrapped("Memory heap size: %s", toBinarySizeString(memoryHeap.size).c_str());
}

//**********************************************************************************************************************
static void renderBuffers(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string bufferName;
	auto buffers = GraphicsAPI::bufferPool.getData();
	renderItemList(GraphicsAPI::bufferPool.getCount(),
		GraphicsAPI::bufferPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive,
		buffers, sizeof(Buffer), bufferName, "Buffer ");

	auto& buffer = buffers[selectedItem];
	auto isMappable = buffer.isMappable();
	VmaAllocationInfo allocationInfo = {};
	vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
	getAllocationInfo(allocationInfo, memoryType, memoryHeap, buffer);

	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(bufferName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Bind types: { %s }", toStringList(buffer.getBind()).c_str());
	renderMemoryDetails(allocationInfo, memoryType, memoryHeap, buffer);
	ImGui::Checkbox("Mappable", &isMappable);
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImages(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string imageName;
	auto images = GraphicsAPI::imagePool.getData();
	renderItemList(GraphicsAPI::imagePool.getCount(),
		GraphicsAPI::imagePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive,
		images, sizeof(Image), imageName, "Image ");

	auto& image = images[selectedItem];
	auto isSwapchain = image.isSwapchain();
	VmaAllocationInfo allocationInfo = {};
	vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
	getAllocationInfo(allocationInfo, memoryType, memoryHeap, image);

	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(imageName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Image type: %s", toString(image.getType()).data());
	ImGui::TextWrapped("Format type: %s", toString(image.getFormat()).data());
	ImGui::TextWrapped("Bind types: { %s }", toStringList(image.getBind()).c_str());
	ImGui::TextWrapped("Image size: %ldx%ldx%ld", (long)image.getSize().x,
		(long)image.getSize().y, (long)image.getSize().z);
	ImGui::TextWrapped("Layer count: %lu", (unsigned long)image.getLayerCount());
	ImGui::TextWrapped("Mip count: %lu", (unsigned long)image.getMipCount());
	renderMemoryDetails(allocationInfo, memoryType, memoryHeap, image);
	ImGui::Checkbox("Swachain", &isSwapchain);
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImageViews(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string imageViewName;
	auto imageViews = GraphicsAPI::imageViewPool.getData();
	renderItemList(GraphicsAPI::imageViewPool.getCount(),
		GraphicsAPI::imageViewPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive,
		imageViews, sizeof(ImageView), imageViewName, "Image View ");

	auto& imageView = imageViews[selectedItem];
	string imageName;
	if (imageView.getImage())
	{
		auto image = GraphicsAPI::imagePool.get(imageView.getImage());
		imageName = imageView.getDebugName().empty() ? "Image " +
			to_string(*imageView.getImage()) : imageView.getDebugName();
	}
	auto isDefault = imageView.isDefault();

	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(imageViewName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Image: %s", imageName.c_str());
	ImGui::TextWrapped("Image type: %s", toString(imageView.getType()).data());
	ImGui::TextWrapped("Format type: %s", toString(imageView.getFormat()).data());
	ImGui::TextWrapped("Base layer: %lu", (unsigned long)imageView.getBaseLayer());
	ImGui::TextWrapped("Layer count: %lu", (unsigned long)imageView.getLayerCount());
	ImGui::TextWrapped("Base mip: %lu", (unsigned long)imageView.getBaseMip());
	ImGui::TextWrapped("Mip count: %lu", (unsigned long)imageView.getMipCount());
	ImGui::Checkbox("Default", &isDefault);
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderFramebuffers(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string framebufferName;
	auto framebuffers = GraphicsAPI::framebufferPool.getData();
	renderItemList(GraphicsAPI::framebufferPool.getCount(),
		GraphicsAPI::framebufferPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, framebuffers,
		sizeof(Framebuffer), framebufferName, "Framebuffer ");

	auto& framebuffer = framebuffers[selectedItem];
	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(framebufferName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)*GraphicsAPI::framebufferPool.getID(&framebuffer));
	ImGui::TextWrapped("Size: %ldx%ld", (long)framebuffer.getSize().x, (long)framebuffer.getSize().y);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Color attachments"))
	{
		ImGui::Indent();

		auto& colorAttachments = framebuffer.getColorAttachments();
		for (psize i = 0; i < colorAttachments.size(); i++)
		{
			auto attachment = colorAttachments[i];
			auto imageView = GraphicsAPI::imageViewPool.get(attachment.imageView);
			auto viewName = imageView->getDebugName().empty() ? "Image View " +
				to_string(*attachment.imageView) : imageView->getDebugName();
			ImGui::SeparatorText(to_string(i).c_str());
			ImGui::Text("%s", viewName.c_str());
			auto value = attachment.clear;
			ImGui::Checkbox("Clear", &value); ImGui::SameLine();
			value = attachment.load;
			ImGui::Checkbox("Load", &value); ImGui::SameLine();
			value = attachment.store;
			ImGui::Checkbox("Store", &value);
		}

		if (colorAttachments.empty())
			ImGui::TextDisabled("None");
		
		ImGui::Unindent();
		ImGui::Spacing();
	}

	if (ImGui::CollapsingHeader("Depth / Stencil attachment"))
	{
		ImGui::Indent();

		auto depthStencilAttachment = framebuffer.getDepthStencilAttachment();
		if (depthStencilAttachment.imageView)
		{
			auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
			auto viewName = imageView->getDebugName().empty() ? "Image View " +
				to_string(*depthStencilAttachment.imageView) : imageView->getDebugName();
			ImGui::Text("%s", viewName.c_str()); ImGui::SameLine();
			auto value = depthStencilAttachment.clear;
			ImGui::Checkbox("Clear", &value); ImGui::SameLine();
			value = depthStencilAttachment.load;
			ImGui::Checkbox("Load", &value); ImGui::SameLine();
			value = depthStencilAttachment.store;
			ImGui::Checkbox("Store", &value);
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

		auto& subpasses = framebuffer.getSubpasses();
		for (psize i = 0; i < subpasses.size(); i++)
		{
			auto& subpass = subpasses[i];
			ImGui::SeparatorText(to_string(i).c_str());
			ImGui::TextWrapped("Pipeline type: %s", toString(subpass.pipelineType).data());
			ImGui::TextWrapped("Input attachment count: %lu", (unsigned long)subpass.inputAttachments.size());
			ImGui::TextWrapped("Output attachment count: %lu", (unsigned long)subpass.outputAttachments.size());
			// TODO: render input and output attachment list.
		}

		if (subpasses.empty())
			ImGui::TextDisabled("None");
		
		ImGui::Unindent();
		ImGui::Spacing();
	}

	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderDescriptorSets(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string descriptorSetName;
	auto descriptorSets = GraphicsAPI::descriptorSetPool.getData();
	renderItemList(GraphicsAPI::descriptorSetPool.getCount(),
		GraphicsAPI::descriptorSetPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, descriptorSets,
		sizeof(DescriptorSet), descriptorSetName, "Descriptor Set ");

	auto& descriptorSet = descriptorSets[selectedItem];
	string pipelineName;
	if (descriptorSet.getPipeline())
	{
		if (descriptorSet.getPipelineType() == PipelineType::Graphics)
		{
			auto pipeline = GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(descriptorSet.getPipeline()));
			pipelineName = pipeline->getDebugName().empty() ? "Graphics Pipeline " +
				to_string(*descriptorSet.getPipeline()) : pipeline->getDebugName();
		}
		else if (descriptorSet.getPipelineType() == PipelineType::Compute)
		{
			auto pipeline = GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(descriptorSet.getPipeline()));
			pipelineName = pipeline->getDebugName().empty() ? "Compute Pipeline " +
				to_string(*descriptorSet.getPipeline()) : pipeline->getDebugName();
		}
		else abort();
	}

	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(descriptorSetName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Pipeline: %s", pipelineName.c_str());
	ImGui::TextWrapped("Pipeline type: %s", toString(descriptorSet.getPipelineType()).data());
	ImGui::TextWrapped("Index: %lu", (unsigned long)descriptorSet.getIndex());
	ImGui::TextWrapped("Set count: %lu", (unsigned long)descriptorSet.getSetCount());
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Uniforms"))
	{
		ImGui::Indent();

		auto& uniforms = descriptorSet.getUniforms();
		for (auto& pair : uniforms)
			ImGui::TextWrapped("%s", pair.first.c_str());
		// TODO: render resource sets.
		if (uniforms.empty())
			ImGui::TextDisabled("None");

		ImGui::Unindent();
		ImGui::Spacing();
	}

	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderPipelineDetails(const Pipeline& pipeline)
{
	bool useAsyncRecording = pipeline.useAsyncRecording();
	bool isBindless = pipeline.isBindless();
	ImGui::TextWrapped("Path: %s", pipeline.getPath().generic_string().c_str());
	ImGui::TextWrapped("Variant count: %lu", (unsigned long)pipeline.getVariantCount());
	ImGui::TextWrapped("Push constants size: %lu", (unsigned long)pipeline.getPushConstantsSize());
	ImGui::TextWrapped("Max bindless count: %lu", (unsigned long)pipeline.getMaxBindlessCount());
	ImGui::Checkbox("Async recording", &useAsyncRecording);
	ImGui::Checkbox("Bindless", &isBindless);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Uniforms"))
	{
		ImGui::Indent();

		auto& uniforms = pipeline.getUniforms();
		for (auto& pair : uniforms)
		{
			auto uniform = pair.second;
			auto readAccess = uniform.readAccess;
			auto writeAccess = uniform.writeAccess;
			ImGui::SeparatorText(pair.first.c_str());
			ImGui::TextWrapped("Type: %s", toString(uniform.type).data());
			ImGui::TextWrapped("Shader stages: %s", toStringList(uniform.shaderStages).data());
			ImGui::TextWrapped("Binding index: %lu", (unsigned long)uniform.bindingIndex);
			ImGui::TextWrapped("Descriptor set index: %lu", (unsigned long)uniform.descriptorSetIndex);
			ImGui::TextWrapped("Array size: %lu", (unsigned long)uniform.arraySize);
			ImGui::Checkbox("Read access", &readAccess);
			ImGui::Checkbox("Write access", &writeAccess);
		}

		if (uniforms.empty())
			ImGui::TextDisabled("None");

		ImGui::Unindent();
		ImGui::Spacing();
	}
}

//**********************************************************************************************************************
static void renderGraphicsPipelines(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string graphicsPipelineName;
	auto graphicsPipelines = GraphicsAPI::graphicsPipelinePool.getData();
	renderItemList(GraphicsAPI::graphicsPipelinePool.getCount(),
		GraphicsAPI::graphicsPipelinePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, graphicsPipelines,
		sizeof(GraphicsPipeline), graphicsPipelineName, "Graphics Pipeline ");

	auto& graphicsPipeline = graphicsPipelines[selectedItem];
	string framebufferName;
	if (graphicsPipeline.getFramebuffer())
	{
		auto framebuffer = GraphicsAPI::framebufferPool.get(graphicsPipeline.getFramebuffer());
		framebufferName = graphicsPipeline.getDebugName().empty() ? "Framebuffer " +
			to_string(*graphicsPipeline.getFramebuffer()) : graphicsPipeline.getDebugName();
	}

	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(graphicsPipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Framebuffer: %s", framebufferName.c_str());
	ImGui::TextWrapped("Subpass index: %lu", (unsigned long)graphicsPipeline.getSubpassIndex());
	renderPipelineDetails(graphicsPipeline);
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderComputePipelines(uint32& selectedItem, string& resourceSearch, bool& searchCaseSensitive)
{
	string computePipelineName;
	auto computePipelines = GraphicsAPI::computePipelinePool.getData();
	renderItemList(GraphicsAPI::computePipelinePool.getCount(),
		GraphicsAPI::computePipelinePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, computePipelines,
		sizeof(ComputePipeline), computePipelineName, "Compute Pipeline ");

	auto& computePipeline = computePipelines[selectedItem];
	ImGui::BeginChild("##itemView", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + 4)));
	ImGui::SeparatorText(computePipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Local size: %ldx%ldx%ld", (long)computePipeline.getLocalSize().x,
		(long)computePipeline.getLocalSize().y, (long)computePipeline.getLocalSize().z);
	renderPipelineDetails(computePipeline);
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
void GpuResourceEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(750, 450), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("GPU Resource Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll))
		{
			if (ImGui::BeginTabItem("Buffers"))
			{
				renderBuffers(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Images"))
			{
				renderImages(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Image Views"))
			{
				renderImageViews(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Framebuffers"))
			{
				renderFramebuffers(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Descriptor Sets"))
			{
				renderDescriptorSets(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Graphics Pipelines"))
			{
				renderGraphicsPipelines(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Compute Pipelines"))
			{
				renderComputePipelines(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
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
#endif