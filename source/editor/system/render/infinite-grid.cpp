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
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/oit.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static DescriptorSet::Uniforms getUniforms()
{
	DescriptorSet::Uniforms uniforms =
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
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (Manager::Instance::get()->has<OitRenderSystem>())
	{
		ECSM_SUBSCRIBE_TO_EVENT("PreOitRender", InfiniteGridEditorSystem::preRender);
		ECSM_SUBSCRIBE_TO_EVENT("OitRender", InfiniteGridEditorSystem::render);

		pipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/infinite-grid/oit", deferredSystem->getOitFramebuffer(), true);
	}
	else
	{
		ECSM_SUBSCRIBE_TO_EVENT("PreMetaLdrRender", InfiniteGridEditorSystem::preRender);
		ECSM_SUBSCRIBE_TO_EVENT("MetaLdrRender", InfiniteGridEditorSystem::render);

		pipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/infinite-grid/translucent", deferredSystem->getMetaLdrFramebuffer());
	}
	
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("infiniteGrid.isEnabled", isEnabled);
		settingsSystem->getBool("infiniteGrid.isHorizontal", isHorizontal);
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

		if (Manager::Instance::get()->has<OitRenderSystem>())
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreOitRender", InfiniteGridEditorSystem::preRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("OitRender", InfiniteGridEditorSystem::render);
		}
		else
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreMetaLdrRender", InfiniteGridEditorSystem::preRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("MetaLdrRender", InfiniteGridEditorSystem::render);
		}
		
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InfiniteGridEditorSystem::swapchainRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::preRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->camera)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (pipelineView->isReady() && !descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.infiniteGrid");
	}
}
void InfiniteGridEditorSystem::render()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->camera)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

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
	auto swapchainIndex = graphicsSystem->getSwapchainIndex();

	SET_GPU_DEBUG_LABEL("Infinite Grid", Color::transparent);
	if (graphicsSystem->isCurrentRenderPassAsync())
	{
		pipelineView->bindAsync(0);
		pipelineView->setViewportScissorAsync(f32x4::zero, 0);
		pipelineView->bindDescriptorSetAsync(descriptorSet, swapchainIndex, 0);
		pipelineView->pushConstantsAsync(0);
		pipelineView->drawFullscreenAsync(0);
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, swapchainIndex);
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
	}
}

void InfiniteGridEditorSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount && descriptorSet)
	{
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.infiniteGrid");
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Infinite Grid"))
	{
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		ImGui::Indent();

		if (ImGui::Checkbox("Enabled", &isEnabled))
		{
			if (settingsSystem)
				settingsSystem->setBool("infiniteGrid.isEnabled", isEnabled);
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Horizontal", &isHorizontal))
		{
			if (settingsSystem)
				settingsSystem->setBool("infiniteGrid.isHorizontal", isHorizontal);
		}

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