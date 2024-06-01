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

#include "garden/editor/system/render/gpu-resource.hpp"

#if GARDEN_EDITOR
#include "garden/file.hpp"

using namespace garden;

// TODO: Read back buffer and images data. But we need somehow detect which queue uses it to prevent sync problems.
//       maybe iterate command buffers and detect where it used? Also we can iterate command buffers to detect invalid sync.

//**********************************************************************************************************************
GpuResourceEditorSystem::GpuResourceEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", GpuResourceEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", GpuResourceEditorSystem::deinit);
}
GpuResourceEditorSystem::~GpuResourceEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", GpuResourceEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", GpuResourceEditorSystem::deinit);
	}
}

void GpuResourceEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", GpuResourceEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", GpuResourceEditorSystem::editorBarTool);
}
void GpuResourceEditorSystem::deinit()
{
	auto manager = Manager::getInstance();
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
	ImGui::TextWrapped("Memory type index: %lu", (long unsigned)allocationInfo.memoryType);
	ImGui::TextWrapped("Memory heap index: %lu", (long unsigned)memoryType.heapIndex);
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

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::bufferPool.getCount() == 0 || selectedItem >= GraphicsAPI::bufferPool.getOccupancy() ||
		ResourceExt::getInstance(buffers[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& buffer = buffers[selectedItem];
	auto isMappable = buffer.isMappable();
	VmaAllocationInfo allocationInfo = {};
	vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
	getAllocationInfo(allocationInfo, memoryType, memoryHeap, buffer);

	ImGui::SeparatorText(bufferName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Bind types: { %s }", toStringList(buffer.getBind()).c_str());
	renderMemoryDetails(allocationInfo, memoryType, memoryHeap, buffer);
	ImGui::Checkbox("Mappable", &isMappable);
	ImGui::Spacing();

	// TODO: using descriptor sets.

	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImages(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string imageName;
	auto images = GraphicsAPI::imagePool.getData();
	renderItemList(GraphicsAPI::imagePool.getCount(),
		GraphicsAPI::imagePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive,
		images, sizeof(Image), imageName, "Image ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::imagePool.getCount() == 0 || selectedItem >= GraphicsAPI::imagePool.getOccupancy() ||
		ResourceExt::getInstance(images[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& image = images[selectedItem];
	auto isSwapchain = image.isSwapchain();
	VmaAllocationInfo allocationInfo = {};
	vk::MemoryType memoryType = {}; vk::MemoryHeap memoryHeap = {};
	getAllocationInfo(allocationInfo, memoryType, memoryHeap, image);

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
	ImGui::Checkbox("Swapchain", &isSwapchain);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Image Views"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto imageViews = GraphicsAPI::imageViewPool.getData();
		auto occupancy = GraphicsAPI::imageViewPool.getOccupancy();
		auto selectedImage = GraphicsAPI::imagePool.getID(&image);

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			auto& imageView = imageViews[i];
			if (imageView.getImage() != selectedImage)
				continue;
			
			auto imageViewName = imageView.getDebugName().empty() ? "Image View " +
				to_string(i + 1) : imageView.getDebugName();
			isAny = true;

			if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
					selectedItem = i;
					ImGui::TreePop();
					break;
				}
				ImGui::TreePop();
			}
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
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderImageViews(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string imageViewName;
	auto imageViews = GraphicsAPI::imageViewPool.getData();
	renderItemList(GraphicsAPI::imageViewPool.getCount(),
		GraphicsAPI::imageViewPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive,
		imageViews, sizeof(ImageView), imageViewName, "Image View ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::imageViewPool.getCount() == 0 || selectedItem >= GraphicsAPI::imageViewPool.getOccupancy() ||
		ResourceExt::getInstance(imageViews[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& imageView = imageViews[selectedItem];
	string imageName;
	if (imageView.getImage())
	{
		auto image = GraphicsAPI::imagePool.get(imageView.getImage());
		imageName = image->getDebugName().empty() ? "Image " +
			to_string(*imageView.getImage()) : image->getDebugName();
	}
	auto isDefault = imageView.isDefault();

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
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Child Framebuffers"))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto framebuffers = GraphicsAPI::framebufferPool.getData();
		auto occupancy = GraphicsAPI::framebufferPool.getOccupancy();
		auto selectedImageView = GraphicsAPI::imageViewPool.getID(&imageView);

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			auto& framebuffer = framebuffers[i];
			auto& colorAttachments = framebuffer.getColorAttachments();

			auto isFound = false;
			for (uint32 j = 0; j < colorAttachments.size(); j++)
			{
				auto& colorAttachment = colorAttachments[j];
				if (colorAttachment.imageView == selectedImageView)
				{
					isFound = true;
					break;
				}
			}

			auto& depthStencilAttachment = framebuffer.getDepthStencilAttachment();
			if (depthStencilAttachment.imageView == selectedImageView)
				isFound = true;

			if (!isFound)
				continue;

			auto framebufferName = framebuffer.getDebugName().empty() ? "Framebuffer " +
				to_string(i + 1) : framebuffer.getDebugName();
			isAny = true;

			if (ImGui::TreeNodeEx(framebufferName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::Framebuffers;
					selectedItem = i;
					ImGui::TreePop();
					break;
				}
				ImGui::TreePop();
			}
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
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderFramebuffers(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string framebufferName;
	auto framebuffers = GraphicsAPI::framebufferPool.getData();
	renderItemList(GraphicsAPI::framebufferPool.getCount(),
		GraphicsAPI::framebufferPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, framebuffers,
		sizeof(Framebuffer), framebufferName, "Framebuffer ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::framebufferPool.getCount() == 0 || selectedItem >= GraphicsAPI::framebufferPool.getOccupancy() ||
		ResourceExt::getInstance(framebuffers[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& framebuffer = framebuffers[selectedItem];
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
			ImGui::Checkbox("Store", &value); ImGui::SameLine();

			ImGui::PushID(viewName.c_str());
			if (ImGui::Button("Select image view")) 
			{
				openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
				selectedItem = *attachment.imageView - 1;
			}
			ImGui::PopID();
		}

		if (colorAttachments.empty())
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
			auto imageView = GraphicsAPI::imageViewPool.get(depthStencilAttachment.imageView);
			auto viewName = imageView->getDebugName().empty() ? "Image View " +
				to_string(*depthStencilAttachment.imageView) : imageView->getDebugName();
			ImGui::Text("%s", viewName.c_str());
			auto value = depthStencilAttachment.clear;
			ImGui::Checkbox("Clear", &value); ImGui::SameLine();
			value = depthStencilAttachment.load;
			ImGui::Checkbox("Load", &value); ImGui::SameLine();
			value = depthStencilAttachment.store;
			ImGui::Checkbox("Store", &value); ImGui::SameLine();

			ImGui::PushID(viewName.c_str());
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
static void renderDescriptorSets(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string descriptorSetName;
	auto descriptorSets = GraphicsAPI::descriptorSetPool.getData();
	renderItemList(GraphicsAPI::descriptorSetPool.getCount(),
		GraphicsAPI::descriptorSetPool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, descriptorSets,
		sizeof(DescriptorSet), descriptorSetName, "Descriptor Set ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::descriptorSetPool.getCount() == 0 ||
		selectedItem >= GraphicsAPI::descriptorSetPool.getOccupancy() ||
		ResourceExt::getInstance(descriptorSets[selectedItem]) == nullptr)
	{
		ImGui::TextWrapped("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& descriptorSet = descriptorSets[selectedItem];
	string pipelineName; const std::map<string, Pipeline::Uniform>* uniforms;
	if (descriptorSet.getPipeline())
	{
		if (descriptorSet.getPipelineType() == PipelineType::Graphics)
		{
			auto pipeline = GraphicsAPI::graphicsPipelinePool.get(
				ID<GraphicsPipeline>(descriptorSet.getPipeline()));
			pipelineName = pipeline->getDebugName().empty() ? "Graphics Pipeline " +
				to_string(*descriptorSet.getPipeline()) : pipeline->getDebugName();
			uniforms = &pipeline->getUniforms();
		}
		else if (descriptorSet.getPipelineType() == PipelineType::Compute)
		{
			auto pipeline = GraphicsAPI::computePipelinePool.get(
				ID<ComputePipeline>(descriptorSet.getPipeline()));
			pipelineName = pipeline->getDebugName().empty() ? "Compute Pipeline " +
				to_string(*descriptorSet.getPipeline()) : pipeline->getDebugName();
			uniforms = &pipeline->getUniforms();
		}
		else abort();
	}

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

		auto& descriptorUniforms = descriptorSet.getUniforms();
		for (auto& pair : descriptorUniforms)
		{
			auto uniform = uniforms->find(pair.first);
			if (uniform == uniforms->end() ||
				uniform->second.descriptorSetIndex != descriptorSet.getIndex())
			{
				continue;
			}

			ImGui::SeparatorText(pair.first.c_str());

			auto type = uniform->second.type;
			auto& resourceSets = pair.second.resourceSets;
			if (isBufferType(type))
			{
				for (uint32 i = 0; i < (uint32)resourceSets.size(); i++)
				{
					if (ImGui::TreeNodeEx(to_string(i).c_str()))
					{
						auto& resourceArray = resourceSets[i];
						for (auto resource : resourceArray)
						{
							auto bufferView = GraphicsAPI::bufferPool.get(ID<Buffer>(resource));
							auto imageViewName = bufferView->getDebugName().empty() ? "Buffer " +
								to_string(*resource) : bufferView->getDebugName();
							if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
							{
								if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								{
									openNextTab = GpuResourceEditorSystem::TabType::Buffers;
									selectedItem = *resource - 1;
									ImGui::TreePop();
									break;
								}
								ImGui::TreePop();
							}
						}
						ImGui::TreePop();
					}
				}
				
			}
			else if (isSamplerType(type) || isImageType(type))
			{
				for (uint32 i = 0; i < (uint32)resourceSets.size(); i++)
				{
					if (ImGui::TreeNodeEx(to_string(i).c_str()))
					{
						auto& resourceArray = resourceSets[i];
						for (auto resource : resourceArray)
						{
							auto imageView = GraphicsAPI::imageViewPool.get(ID<ImageView>(resource));
							auto imageViewName = imageView->getDebugName().empty() ? "Image View " +
								to_string(*resource) : imageView->getDebugName();
							if (ImGui::TreeNodeEx(imageViewName.c_str(), ImGuiTreeNodeFlags_Leaf))
							{
								if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								{
									openNextTab = GpuResourceEditorSystem::TabType::ImageViews;
									selectedItem = *resource - 1;
									ImGui::TreePop();
									break;
								}
								ImGui::TreePop();
							}
						}
						ImGui::TreePop();
					}
				}
			}
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
	renderSearch(resourceSearch, searchCaseSensitive);
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
			ImGui::Checkbox("Read access", &readAccess); ImGui::SameLine();
			ImGui::Checkbox("Write access", &writeAccess);
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

		auto descriptorSets = GraphicsAPI::descriptorSetPool.getData();
		auto occupancy = GraphicsAPI::descriptorSetPool.getOccupancy();

		auto isAny = false;
		for (uint32 i = 0; i < occupancy; i++)
		{
			auto& descriptorSet = descriptorSets[i];
			if (descriptorSet.getPipelineType() != pipeline.getType() ||
				descriptorSet.getPipeline() != instance)
			{
				continue;
			}

			auto descriptorSetName = descriptorSet.getDebugName().empty() ? "Descriptor Set " +
				to_string(i + 1) : descriptorSet.getDebugName();
			isAny = true;

			if (ImGui::TreeNodeEx(descriptorSetName.c_str(), ImGuiTreeNodeFlags_Leaf))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					openNextTab = GpuResourceEditorSystem::TabType::DescriptorSets;
					selectedItem = i;
					ImGui::TreePop();
					break;
				}
				ImGui::TreePop();
			}
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
static void renderGraphicsPipelines(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string graphicsPipelineName;
	auto graphicsPipelines = GraphicsAPI::graphicsPipelinePool.getData();
	renderItemList(GraphicsAPI::graphicsPipelinePool.getCount(),
		GraphicsAPI::graphicsPipelinePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, graphicsPipelines,
		sizeof(GraphicsPipeline), graphicsPipelineName, "Graphics Pipeline ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::graphicsPipelinePool.getCount() == 0 ||
		selectedItem >= GraphicsAPI::graphicsPipelinePool.getOccupancy() ||
		ResourceExt::getInstance(graphicsPipelines[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& graphicsPipeline = graphicsPipelines[selectedItem];
	string framebufferName;
	if (graphicsPipeline.getFramebuffer())
	{
		auto framebuffer = GraphicsAPI::framebufferPool.get(graphicsPipeline.getFramebuffer());
		framebufferName = framebuffer->getDebugName().empty() ? "Framebuffer " +
			to_string(*graphicsPipeline.getFramebuffer()) : framebuffer->getDebugName();
	}

	ImGui::SeparatorText(graphicsPipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Framebuffer: %s", framebufferName.c_str());
	ImGui::TextWrapped("Subpass index: %lu", (unsigned long)graphicsPipeline.getSubpassIndex());
	auto instance = ID<Pipeline>(GraphicsAPI::graphicsPipelinePool.getID(&graphicsPipeline));
	renderPipelineDetails(graphicsPipeline, instance, openNextTab, selectedItem);
	ImGui::Spacing();
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
static void renderComputePipelines(uint32& selectedItem, string& resourceSearch,
	bool& searchCaseSensitive, GpuResourceEditorSystem::TabType& openNextTab)
{
	string computePipelineName;
	auto computePipelines = GraphicsAPI::computePipelinePool.getData();
	renderItemList(GraphicsAPI::computePipelinePool.getCount(),
		GraphicsAPI::computePipelinePool.getOccupancy(),
		selectedItem, resourceSearch, searchCaseSensitive, computePipelines,
		sizeof(ComputePipeline), computePipelineName, "Compute Pipeline ");

	ImGui::BeginChild("##itemView", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)));
	if (GraphicsAPI::computePipelinePool.getCount() == 0 ||
		selectedItem >= GraphicsAPI::computePipelinePool.getOccupancy() ||
		ResourceExt::getInstance(computePipelines[selectedItem]) == nullptr)
	{
		ImGui::TextDisabled("None");
		ImGui::EndChild();
		renderSearch(resourceSearch, searchCaseSensitive);
		return;
	}

	auto& computePipeline = computePipelines[selectedItem];
	ImGui::SeparatorText(computePipelineName.c_str());
	ImGui::TextWrapped("Runtime ID: %lu", (unsigned long)(selectedItem + 1));
	ImGui::TextWrapped("Local size: %ldx%ldx%ld", (long)computePipeline.getLocalSize().x,
		(long)computePipeline.getLocalSize().y, (long)computePipeline.getLocalSize().z);
	auto instance = ID<Pipeline>(GraphicsAPI::computePipelinePool.getID(&computePipeline));
	renderPipelineDetails(computePipeline, instance, openNextTab, selectedItem);
	ImGui::Spacing();
	ImGui::EndChild();
	renderSearch(resourceSearch, searchCaseSensitive);
}

//**********************************************************************************************************************
void GpuResourceEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
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
				renderBuffers(selectedItem, resourceSearch, searchCaseSensitive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Images", nullptr, openNextTab == 
				TabType::Images ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderImages(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Image Views", nullptr, openNextTab == 
				TabType::ImageViews ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderImageViews(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Framebuffers", nullptr, openNextTab == 
				TabType::Framebuffers ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderFramebuffers(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Descriptor Sets", nullptr, openNextTab == 
				TabType::DescriptorSets ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderDescriptorSets(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Graphics Pipelines", nullptr, openNextTab == 
				TabType::GraphicsPipelines ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderGraphicsPipelines(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Compute Pipelines", nullptr, openNextTab == 
				TabType::ComputePipelines ? ImGuiTabItemFlags_SetSelected : 0))
			{
				renderComputePipelines(selectedItem, resourceSearch, searchCaseSensitive, openNextTab);
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
	selectedItem = *resource - 1;
	openNextTab = type;
	showWindow = true;
}
#endif