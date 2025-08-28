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
#include "garden/system/render/forward.hpp"
#include "garden/system/render/oit.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static DescriptorSet::Uniforms getUniforms()
{
	return { { "cc", DescriptorSet::Uniform(GraphicsSystem::Instance::get()->getCommonConstantsBuffers()) } };
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
	if (OitRenderSystem::Instance::has())
	{
		ECSM_SUBSCRIBE_TO_EVENT("PreOitRender", InfiniteGridEditorSystem::preRender);
		ECSM_SUBSCRIBE_TO_EVENT("OitRender", InfiniteGridEditorSystem::render);
	}
	else
	{
		if (DeferredRenderSystem::Instance::has())
		{
			ECSM_SUBSCRIBE_TO_EVENT("PreDepthLdrRender", InfiniteGridEditorSystem::preRender);
			ECSM_SUBSCRIBE_TO_EVENT("DepthLdrRender", InfiniteGridEditorSystem::render);
		}
		else
		{
			ECSM_SUBSCRIBE_TO_EVENT("PreDepthForwardRender", InfiniteGridEditorSystem::preRender);
			ECSM_SUBSCRIBE_TO_EVENT("DepthForwardRender", InfiniteGridEditorSystem::render);
		}
	}

	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getBool("infiniteGrid.enabled", isEnabled);
		settingsSystem->getBool("infiniteGrid.horizontal", isHorizontal);
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

		if (OitRenderSystem::Instance::has())
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreOitRender", InfiniteGridEditorSystem::preRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("OitRender", InfiniteGridEditorSystem::render);
		}
		else
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreDepthLdrRender", InfiniteGridEditorSystem::preRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("DepthLdrRender", InfiniteGridEditorSystem::render);
		}

		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", InfiniteGridEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::preRender()
{
	if (!isEnabled)
		return;

	if (!pipeline)
	{
		if (OitRenderSystem::Instance::has())
		{
			auto deferredSystem = DeferredRenderSystem::Instance::get();
			ResourceSystem::GraphicsOptions options;
			options.useAsyncRecording = true;

			pipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
				"editor/infinite-grid/oit", deferredSystem->getOitFramebuffer(), options);
		}
		else
		{
			ID<Framebuffer> framebuffer; bool useAsyncRecording;
			if (DeferredRenderSystem::Instance::has())
			{
				framebuffer = DeferredRenderSystem::Instance::get()->getDepthLdrFramebuffer();
			}
			else
			{
				auto forwardSystem = ForwardRenderSystem::Instance::get();
				framebuffer = forwardSystem->getFullFramebuffer();
				useAsyncRecording = forwardSystem->useAsyncRecording();
			}

			ResourceSystem::GraphicsOptions options;
			options.useAsyncRecording = useAsyncRecording;

			pipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
				"editor/infinite-grid/translucent", framebuffer, options);
		}
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.infiniteGrid");
	}

	DeferredRenderSystem::Instance::get()->markAnyOIT();
}
void InfiniteGridEditorSystem::render()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	PushConstants pc;
	pc.meshColor = (float4)meshColor;
	pc.meshScale = meshScale;
	pc.isHorizontal = isHorizontal;

	if (isHorizontal)
	{
		pc.axisColorX = (float4)axisColorYZ;
		pc.axisColorYZ = (float4)axisColorX;
	}
	else
	{
		pc.axisColorX = (float4)axisColorX;
		pc.axisColorYZ = (float4)axisColorYZ;
	}

	auto inFlightIndex = graphicsSystem->getInFlightIndex();

	SET_GPU_DEBUG_LABEL("Infinite Grid", Color::transparent);
	if (graphicsSystem->isCurrentRenderPassAsync())
	{
		pipelineView->bindAsync(0);
		pipelineView->setViewportScissorAsync(float4::zero, 0);
		pipelineView->bindDescriptorSetAsync(descriptorSet, inFlightIndex, 0);
		pipelineView->pushConstantsAsync(&pc, 0);
		pipelineView->drawFullscreenAsync(0);
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, inFlightIndex);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Infinite Grid"))
	{
		ImGui::Indent();
		ImGui::PushID("infiniteGrid");

		auto settingsSystem = SettingsSystem::Instance::tryGet();
		if (ImGui::Checkbox("Enabled", &isEnabled))
		{
			if (settingsSystem)
				settingsSystem->setBool("infiniteGrid.enabled", isEnabled);
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Horizontal", &isHorizontal))
		{
			if (settingsSystem)
				settingsSystem->setBool("infiniteGrid.horizontal", isHorizontal);
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

		ImGui::PopID();
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif