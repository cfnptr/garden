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

#include "garden/editor/system/render/infinite-grid.hpp"

#if GARDEN_EDITOR
#include "math/tone-mapping.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static map<string, DescriptorSet::Uniform> getUniforms()
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "cc", DescriptorSet::Uniform(GraphicsSystem::Instance::get()->getCameraConstantsBuffers()) } };
	return uniforms;
}

//**********************************************************************************************************************
InfiniteGridEditorSystem::InfiniteGridEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", InfiniteGridEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
}
InfiniteGridEditorSystem::~InfiniteGridEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", InfiniteGridEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);

	auto swapchainFramebuffer = GraphicsSystem::Instance::get()->getSwapchainFramebuffer();
	pipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline("editor/infinite-grid", swapchainFramebuffer);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getFloat("infiniteGrid.meshScale", meshScale);
		settingsSystem->getColor("infiniteGrid.meshColor", meshColor);
		settingsSystem->getColor("infiniteGrid.axisColorX", axisColorX);
		settingsSystem->getColor("infiniteGrid.axisColorYZ", axisColorYZ);
	}
}
void InfiniteGridEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.infinite-grid");
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());

	graphicsSystem->startRecording(CommandBufferType::Frame);
 	{
		auto pushConstants = pipelineView->getPushConstants<PushConstants>();
		pushConstants->meshColor = (float4)meshColor;
		if (isHorizontal)
		{
			pushConstants->axisColorX = (float4)axisColorYZ;
			pushConstants->axisColorYZ = (float4)axisColorX;
		}
		else
		{
			pushConstants->axisColorX = (float4)axisColorX;
			pushConstants->axisColorYZ = (float4)axisColorYZ;
		}
		pushConstants->meshScale = meshScale;
		pushConstants->isHorizontal = isHorizontal;

		SET_GPU_DEBUG_LABEL("Infinite Grid", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, graphicsSystem->getSwapchainIndex());
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}

void InfiniteGridEditorSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount && descriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.infinite-grid");
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Infinite Grid"))
	{
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		ImGui::Indent();
		ImGui::Checkbox("Enabled", &isEnabled); ImGui::SameLine();
		ImGui::Checkbox("Horizontal", &isHorizontal);
		if (ImGui::DragFloat("Mesh Scale", &meshScale, 0.1f))
		{
			if (settingsSystem)
				settingsSystem->setFloat("infiniteGrid.meshScale", meshScale);
		}
		if (ImGui::ColorEdit4("Mesh Color", &meshColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("infiniteGrid.meshColor", meshColor);
		}
		if (ImGui::ColorEdit4("X-Axis Color", &axisColorX))
		{
			if (settingsSystem)
				settingsSystem->setColor("infiniteGrid.axisColorX", axisColorX);
		}
		auto axisName = isHorizontal ? "Z-Axis Color" : "Y-Axis Color";
		if (ImGui::ColorEdit4(axisName, &axisColorYZ))
		{
			if (settingsSystem)
				settingsSystem->setColor("infiniteGrid.axisColorYZ", axisColorYZ);
		}
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif