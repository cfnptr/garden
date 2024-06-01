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

#include "garden/editor/system/render/infinite-grid.hpp"

#if GARDEN_EDITOR
#include "math/tone-mapping.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4 gridColor;
		float4 axisColorX;
		float4 axisColorY;
		float gridScale;
		int32 isHorizontal;
	};
}

static map<string, DescriptorSet::Uniform> getUniforms()
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "cc", DescriptorSet::Uniform(GraphicsSystem::getInstance()->getCameraConstantsBuffers()) } };
	return uniforms;
}

//**********************************************************************************************************************
InfiniteGridEditorSystem::InfiniteGridEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", InfiniteGridEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
}
InfiniteGridEditorSystem::~InfiniteGridEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", InfiniteGridEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
	SUBSCRIBE_TO_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);

	auto swapchainFramebuffer = GraphicsSystem::getInstance()->getSwapchainFramebuffer();
	pipeline = ResourceSystem::getInstance()->loadGraphicsPipeline("editor/infinite-grid", swapchainFramebuffer);

	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem)
	{
		settingsSystem->getFloat("infGridScale", gridScale);
		settingsSystem->getColor("infGridColor", gridColor);
		settingsSystem->getColor("infGridColorX", axisColorX);
		settingsSystem->getColor("infGridColorY", axisColorY);
	}
}
void InfiniteGridEditorSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);

		UNSUBSCRIBE_FROM_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
		UNSUBSCRIBE_FROM_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet, "descriptorSet.infinite-grid");
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());

	graphicsSystem->startRecording(CommandBufferType::Frame);
 	{
		SET_GPU_DEBUG_LABEL("Infinite Grid", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, graphicsSystem->getSwapchainIndex());
		auto pushConstants = pipelineView->getPushConstants<PushConstants>();
		pushConstants->gridColor = (float4)gridColor;
		pushConstants->axisColorX = (float4)axisColorX;
		pushConstants->axisColorY = (float4)axisColorY;
		pushConstants->gridScale = gridScale;
		pushConstants->isHorizontal = isHorizontal;
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}

void InfiniteGridEditorSystem ::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount && descriptorSet)
	{
		auto uniforms = getUniforms();
		auto descriptorSetView = graphicsSystem->get(descriptorSet);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Infinite Grid"))
	{
		auto settingsSystem = Manager::getInstance()->tryGet<SettingsSystem>();
		ImGui::Indent();
		ImGui::Checkbox("Enabled", &isEnabled); ImGui::SameLine();
		ImGui::Checkbox("Horizontal", &isHorizontal);
		if (ImGui::DragFloat("Grid Scale", &gridScale, 0.1f))
		{
			if (settingsSystem)
				settingsSystem->setFloat("infGridScale", gridScale);
		}
		if (ImGui::ColorEdit4("Grid Color", &gridColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("infGridColor", gridColor);
		}
		if (ImGui::ColorEdit4("X-Axis Color", &axisColorX))
		{
			if (settingsSystem)
				settingsSystem->setColor("infGridColorX", axisColorX);
		}
		auto axisName = isHorizontal ? "Z-Axis Color" : "Y-Axis Color";
		if (ImGui::ColorEdit4(axisName, &axisColorY))
		{
			if (settingsSystem)
				settingsSystem->setColor("infGridColorY", axisColorY);
		}
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif