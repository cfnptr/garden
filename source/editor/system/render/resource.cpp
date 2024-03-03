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

// TODO: refactor resource editor

#include "garden/editor/system/render/resource.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#include "garden/file.hpp"

using namespace garden;

//**********************************************************************************************************************
ResourceEditorSystem::ResourceEditorSystem(Manager* manager,
	EditorRenderSystem* system) : EditorSystem(manager, system)
{
	SUBSCRIBE_TO_EVENT("RenderEditor", ResourceEditorSystem::renderEditor);
	SUBSCRIBE_TO_EVENT("EditorBarTool", ResourceEditorSystem::editorBarTool);
}
ResourceEditorSystem::~ResourceEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("RenderEditor", ResourceEditorSystem::renderEditor);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", ResourceEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void ResourceEditorSystem::renderEditor()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;
	
	if (ImGui::Begin("Resource Viewer", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginTable("ViewerResources1", 4, ImGuiTableFlags_Borders);
		ImGui::TableSetupColumn("Buffers");
		ImGui::TableSetupColumn("Buffer Views");
		ImGui::TableSetupColumn("Images");
		ImGui::TableSetupColumn("Image Views");
		ImGui::TableHeadersRow(); ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::bufferPool.getCount());
		ImGui::TableNextColumn(); ImGui::Text("%d", 0); // TODO:
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::imagePool.getCount());
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::imageViewPool.getCount());
		ImGui::EndTable();

		ImGui::BeginTable("ViewerResources2", 3, ImGuiTableFlags_Borders);
		ImGui::TableSetupColumn("Graphics Pipelines");
		ImGui::TableSetupColumn("Compute Pipelines");
		ImGui::TableSetupColumn("Raytracing Pipelines");
		ImGui::TableHeadersRow(); ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::graphicsPipelinePool.getCount());
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::computePipelinePool.getCount());
		ImGui::TableNextColumn(); ImGui::Text("%d", 0); // TODO:
		ImGui::EndTable();

		ImGui::BeginTable("ViewerResources3", 4, ImGuiTableFlags_Borders);
		ImGui::TableSetupColumn("Framebuffers");
		ImGui::TableSetupColumn("Descriptor Sets");
		ImGui::TableHeadersRow(); ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::framebufferPool.getCount());
		ImGui::TableNextColumn(); ImGui::Text("%d", GraphicsAPI::descriptorSetPool.getCount());
		ImGui::EndTable();

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Buffers"))
		{
			ImGui::BeginChild("ViewerBuffers", ImVec2(0.0f, 192.0f));
			auto buffers = GraphicsAPI::bufferPool.getData();
			auto occupancy = GraphicsAPI::bufferPool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& buffer = buffers[i];
				if (buffer.getBinarySize() == 0)
					continue;

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
			auto images = GraphicsAPI::imagePool.getData();
			auto occupancy = GraphicsAPI::imagePool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& image = images[i];
				if (image.getBinarySize() == 0)
					continue;

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
			auto imageViews = GraphicsAPI::imageViewPool.getData();
			auto occupancy = GraphicsAPI::imageViewPool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& imageView = imageViews[i];
				if (!imageView.getImage())
					continue;

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
			auto graphicsPipelines = GraphicsAPI::graphicsPipelinePool.getData();
			auto occupancy = GraphicsAPI::graphicsPipelinePool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& graphicsPipeline = graphicsPipelines[i];
				if (!graphicsPipeline.getFramebuffer())
					continue;

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
			auto computePipelines = GraphicsAPI::computePipelinePool.getData();
			auto occupancy = GraphicsAPI::computePipelinePool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& computePipeline = computePipelines[i];
				if (computePipeline.getLocalSize() == 0)
					continue;

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
			auto framebuffers = GraphicsAPI::framebufferPool.getData();
			auto occupancy = GraphicsAPI::framebufferPool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& framebuffer = framebuffers[i];
				auto size = framebuffer.getSize();
				if (size == 0)
					continue;

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
			auto descriptorSets = GraphicsAPI::descriptorSetPool.getData();
			auto occupancy = GraphicsAPI::descriptorSetPool.getOccupancy();
			for (uint32 i = 0; i < occupancy; i++)
			{
				auto& descriptorSet = descriptorSets[i];
				if (!descriptorSet.getPipeline())
					continue;

				ImGui::Text("ID: %d, Name: %s", i, descriptorSet.getDebugName().c_str());
				// TODO:
				ImGui::Separator();
			}
			ImGui::EndChild(); ImGui::Spacing();
		}
	}
	ImGui::End();
}

//**********************************************************************************************************************
void ResourceEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Resource Viewer"))
		showWindow = true;
}
#endif