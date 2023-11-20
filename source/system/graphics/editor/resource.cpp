//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/graphics/editor/resource.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
ResourceEditor::ResourceEditor(EditorRenderSystem* system)
{
	system->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void ResourceEditor::render()
{
	if (showWindow)
	{
		if (ImGui::Begin("Resource Viewer", &showWindow,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::BeginTable("ViewerResources1", 4, ImGuiTableFlags_Borders);
			ImGui::TableSetupColumn("Buffers");
			ImGui::TableSetupColumn("Buffer Views");
			ImGui::TableSetupColumn("Images");
			ImGui::TableSetupColumn("Image Views");
			ImGui::TableHeadersRow(); ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::bufferPool.getCount());
			ImGui::TableNextColumn(); ImGui::Text("%d", 0); // TODO:
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::imagePool.getCount());
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::imageViewPool.getCount());
			ImGui::EndTable();

			ImGui::BeginTable("ViewerResources2", 3, ImGuiTableFlags_Borders);
			ImGui::TableSetupColumn("Graphics Pipelines");
			ImGui::TableSetupColumn("Compute Pipelines");
			ImGui::TableSetupColumn("Raytracing Pipelines");
			ImGui::TableHeadersRow(); ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::graphicsPipelinePool.getCount());
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::computePipelinePool.getCount());
			ImGui::TableNextColumn(); ImGui::Text("%d", 0); // TODO:
			ImGui::EndTable();

			ImGui::BeginTable("ViewerResources3", 4, ImGuiTableFlags_Borders);
			ImGui::TableSetupColumn("Framebuffers");
			ImGui::TableSetupColumn("Descriptor Sets");
			ImGui::TableHeadersRow(); ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::framebufferPool.getCount());
			ImGui::TableNextColumn(); ImGui::Text("%d", Vulkan::descriptorSetPool.getCount());
			ImGui::EndTable();

			ImGui::Text("Destroy Resources: %d", (int)Vulkan::destroyBuffer.size());
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Buffers"))
			{
				ImGui::BeginChild("ViewerBuffers", ImVec2(0.0f, 192.0f));
				auto buffers = Vulkan::bufferPool.getData();
				auto occupancy = Vulkan::bufferPool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& buffer = buffers[i];
					if (buffer.getBinarySize() == 0) continue;
					ImGui::Text("ID: %d, Name: %s", i, buffer.getDebugName().c_str());
					ImGui::Text("Size: %s, Usage: %s",
						toBinarySizeString(buffer.getBinarySize()).c_str(),
						toString(buffer.getMemoryUsage()).data());
					ImGui::Text("Bind: [%s]", toStringList(buffer.getBind()).data());
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Buffer Views"))
			{
				ImGui::BeginChild("ViewerBufferViews", ImVec2(0.0f, 192.0f));
				// TODO:
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Images"))
			{
				ImGui::BeginChild("ViewerImages", ImVec2(0.0f, 192.0f));
				auto images = Vulkan::imagePool.getData();
				auto occupancy = Vulkan::imagePool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& image = images[i];
					if (image.getBinarySize() == 0) continue;
					auto& size = image.getSize();
					ImGui::Text("ID: %d, Name: %s", i, image.getDebugName().c_str());
					ImGui::Text("Type: %s, Format: %s", toString(image.getType()).data(),
						toString(image.getFormat()).data());
					ImGui::Text("Size: %dx%dx%d, Mips: %d, Layers: %d",
						size.x, size.y, size.z, image.getMipCount(), image.getLayerCount());
					ImGui::Text("Binary Size: %s, Usage: %s",
						toBinarySizeString(image.getBinarySize()).c_str(),
						toString(image.getMemoryUsage()).data());
					ImGui::Text("Bind: [%s]", toStringList(image.getBind()).data());
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Image Views"))
			{
				ImGui::BeginChild("ViewerImageViews", ImVec2(0.0f, 192.0f));
				auto imageViews = Vulkan::imageViewPool.getData();
				auto occupancy = Vulkan::imageViewPool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& imageView = imageViews[i];
					if (!imageView.getImage()) continue;
					ImGui::Text("ID: %d, Name: %s", i, imageView.getDebugName().c_str());
					ImGui::Text("Type: %s, Format: %s, Image ID: %d",
						toString(imageView.getType()).data(),
						toString(imageView.getFormat()).data(), *imageView.getImage());
					ImGui::Text("Mip: %d / Count: %d, Layer: %d / Count: %d",
						imageView.getBaseMip(), imageView.getMipCount(),
						imageView.getBaseLayer(), imageView.getLayerCount());
					auto isDefault = imageView.isDefault();
					ImGui::Checkbox("Default", &isDefault);
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Graphics Pipelines"))
			{
				ImGui::BeginChild("ViewerGraphicsPipelines", ImVec2(0.0f, 192.0f));
				auto graphicsPipelines = Vulkan::graphicsPipelinePool.getData();
				auto occupancy = Vulkan::graphicsPipelinePool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& graphicsPipeline = graphicsPipelines[i];
					if (!graphicsPipeline.getFramebuffer()) continue;
					auto path = graphicsPipeline.getPath().generic_string();
					ImGui::Text("ID: %d, Path: %s", i, path.c_str());
					// TODO:
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Compute Pipelines"))
			{
				ImGui::BeginChild("ViewerComputePipelines", ImVec2(0.0f, 192.0f));
				auto computePipelines = Vulkan::computePipelinePool.getData();
				auto occupancy = Vulkan::computePipelinePool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& computePipeline = computePipelines[i];
					if (computePipeline.getLocalSize() == 0) continue;
					auto path = computePipeline.getPath().generic_string();
					ImGui::Text("ID: %d, Path: %s", i, path.c_str());
					// TODO:
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Raytracing Pipelines"))
			{
				ImGui::BeginChild("ViewerRaytracingPipelines", ImVec2(0.0f, 192.0f));
				// TODO:
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Framebuffers"))
			{
				ImGui::BeginChild("ViewerFramebuffers", ImVec2(0.0f, 192.0f));
					auto framebuffers = Vulkan::framebufferPool.getData();
				auto occupancy = Vulkan::framebufferPool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& framebuffer = framebuffers[i];
					auto size = framebuffer.getSize();
					if (size == 0) continue;
					ImGui::Text("ID: %d, Name: %s", i, framebuffer.getDebugName().c_str());
					ImGui::Text("Size: %dx%d", size.x, size.y);
					// TODO: attachments
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Descriptor Sets"))
			{
				ImGui::BeginChild("ViewerDescriptorSets", ImVec2(0.0f, 192.0f));
				auto descriptorSets = Vulkan::descriptorSetPool.getData();
				auto occupancy = Vulkan::descriptorSetPool.getOccupancy();
				for (uint32 i = 0; i < occupancy; i++)
				{
					auto& descriptorSet = descriptorSets[i];
					if (!descriptorSet.getPipeline()) continue;
					ImGui::Text("ID: %d, Name: %s", i, descriptorSet.getDebugName().c_str());
					// TODO:
					ImGui::Separator();
				}
				ImGui::EndChild(); ImGui::Spacing();
			}
		}
		ImGui::End();
	}
}

void ResourceEditor::onBarTool()
{
	if (ImGui::MenuItem("Resource Viewer")) showWindow = true;
}
#endif