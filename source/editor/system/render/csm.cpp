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

#include "garden/editor/system/render/csm.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/csm.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static DescriptorSet::Uniforms getCascadesUniforms()
{
	auto depthBufferView = DeferredRenderSystem::Instance::get()->getDepthImageView();
	return { { "depthBuffer", DescriptorSet::Uniform(depthBufferView) } };
}

//**********************************************************************************************************************
CsmRenderEditorSystem::CsmRenderEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", CsmRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CsmRenderEditorSystem::deinit);
}
CsmRenderEditorSystem::~CsmRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CsmRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CsmRenderEditorSystem::deinit);
	}
}

void CsmRenderEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", CsmRenderEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("UiRender", CsmRenderEditorSystem::uiRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CsmRenderEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", CsmRenderEditorSystem::editorBarToolPP);
}
void CsmRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cascadesDS);
		graphicsSystem->destroy(cascadesPipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", CsmRenderEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", CsmRenderEditorSystem::uiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CsmRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", CsmRenderEditorSystem::editorBarToolPP);
	}
}

static uint32 shadowMapTypeToSize(int sizeType) noexcept
{
	switch (sizeType)
	{
		case 1: return 256;
		case 2: return 512;
		case 3: return 1024;
		case 4: return 2048;
		case 5: return 4096;
		default: return 128;
	}
}
static uint32 shadowMapSizeToType(int sizeType) noexcept
{
	switch (sizeType)
	{
		case 256: return 1;
		case 512: return 2;
		case 1024: return 3;
		case 2048: return 4;
		case 4096: return 5;
		default: return 0;
	}
}

//**********************************************************************************************************************
void CsmRenderEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	auto csmSystem = CsmRenderSystem::Instance::get();
	if (ImGui::Begin("Cascade Shadow Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Checkbox("Enabled", &csmSystem->isEnabled); ImGui::SameLine();
		ImGui::Checkbox("Render Tranlucent", &csmSystem->renderTranslucent);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		const auto& cc = graphicsSystem->getCommonConstants();
		auto shadowColor = cc.shadowColor;
		if (ImGui::SliderFloat3("Color", &shadowColor, 0.0f, 1.0f))
			graphicsSystem->setShadowColor((float3)shadowColor, shadowColor.getW());
		if (ImGui::SliderFloat("Alpha", &shadowColor.floats.w, 0.0f, 1.0f))
			graphicsSystem->setShadowColor((float3)shadowColor, shadowColor.getW());

		ImGui::DragFloat("Distance", &csmSystem->distance, 1.0f);
		ImGui::DragFloat("Bias Constant Factor", &csmSystem->biasConstantFactor, 0.01f);
		ImGui::DragFloat("Bias Slope Factor", &csmSystem->biasSlopeFactor, 0.01f);
		ImGui::DragFloat("Bias Normal Factor", &csmSystem->biasNormalFactor, 0.0001f, 0.0f, 0.0f, "%.4f");
		ImGui::DragFloat("Z-Axis Offset Coefficient", &csmSystem->zCoeff, 0.01f);
		ImGui::SliderFloat2("Cascade Splits", &csmSystem->cascadeSplits, 0.001f, 0.999f);

		auto isChanged = false;
		sizeType = shadowMapSizeToType(csmSystem->getShadowMapSize());

		constexpr auto sizeTypes = " Custom\0 256px\0 512px\0 1024px\0 2048px\0 4096px\00";
		if (ImGui::Combo("Map Size", &sizeType, sizeTypes))
		{
			auto size = shadowMapTypeToSize(sizeType);
			if (size > 0)
			{
				csmSystem->setShadowMapSize(size);
				isChanged = true;
			}
		}
		if (sizeType == 0)
		{
			int size = csmSystem->getShadowMapSize();
			if (ImGui::InputInt("Size (px)", &size, 128, 512))
			{
				csmSystem->setShadowMapSize(size);
				isChanged = true;
			}
		}

		if (isChanged)
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setInt("csm.shadowMapSize", csmSystem->getShadowMapSize());
		}

		if (visualizeCascades && !csmSystem->isEnabled)
			visualizeCascades = false;

		ImGui::BeginDisabled(!csmSystem->isEnabled);
		ImGui::Checkbox("Visualize Cascades", &visualizeCascades);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Cascade 1 = green, 2 = yellow, 3 = red, outside = magenta");
			ImGui::EndTooltip();
		}
		ImGui::EndDisabled();

		ImGui::Text("Cascade Far Planes: %f, %f, %f", csmSystem->cascadeSplits.x * csmSystem->distance, 
			csmSystem->cascadeSplits.y * csmSystem->distance, csmSystem->distance);

		if (visualizeCascades)
		{
			if (!cascadesPipeline)
			{
				auto deferredSystem = DeferredRenderSystem::Instance::get();
				ResourceSystem::GraphicsOptions options;
				options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
				cascadesPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/shadow-cascades", deferredSystem->getUiFramebuffer(), options);
			}

			auto pipelineView = graphicsSystem->get(cascadesPipeline);
			if (!pipelineView->isReady())
				ImGui::TextDisabled("Cascades pipeline is loading...");
		}
	}
	ImGui::End();
}
void CsmRenderEditorSystem::uiRender()
{
	if (!visualizeCascades)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(cascadesPipeline);
	if (!pipelineView->isReady())
		return;

	if (!cascadesDS)
	{
		auto uniforms = getCascadesUniforms();
		cascadesDS = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cascadesDS, "descriptorSet.editor.csm.cascades");
	}

	auto csmSystem = CsmRenderSystem::Instance::get();
	const auto& cc = graphicsSystem->getCommonConstants();
	auto isCurrentRenderPassAsync = graphicsSystem->isCurrentRenderPassAsync();
	auto threadIndex = graphicsSystem->getThreadCount() - 1;

	PushConstants pc;
	pc.farPlanes = (float3)(cc.nearPlane / csmSystem->getFarPlanes());

	SET_GPU_DEBUG_LABEL("Shadow Map Cascades");
	if (isCurrentRenderPassAsync)
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(cascadesDS);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
	else
	{
		pipelineView->bindAsync(0, threadIndex);
		pipelineView->setViewportScissorAsync(float4::zero, threadIndex);
		pipelineView->bindDescriptorSetAsync(cascadesDS, 0, threadIndex);
		pipelineView->pushConstantsAsync(&pc, threadIndex);
		pipelineView->drawFullscreenAsync(threadIndex);
	}
}

//**********************************************************************************************************************
void CsmRenderEditorSystem::gBufferRecreate()
{
	if (cascadesDS)
	{
		GraphicsSystem::Instance::get()->destroy(cascadesDS);
		cascadesDS = {};
	}
}

void CsmRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Cascade Shadow Mapping (CSM)"))
		showWindow = true;
}
#endif