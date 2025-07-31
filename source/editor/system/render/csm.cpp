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
	ECSM_SUBSCRIBE_TO_EVENT("Init", CsmRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CsmRenderEditorSystem::deinit);
}
CsmRenderEditorSystem::~CsmRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CsmRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CsmRenderEditorSystem::deinit);
	}
}

void CsmRenderEditorSystem::init()
{
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

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", CsmRenderEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", CsmRenderEditorSystem::uiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CsmRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", CsmRenderEditorSystem::editorBarToolPP);
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
		auto graphicsSystem = GraphicsSystem::Instance::get();
		const auto& cameraConstants = graphicsSystem->getCameraConstants();
		auto shadowColor = cameraConstants.shadowColor;
		if (ImGui::SliderFloat3("Color", &shadowColor, 0.0f, 1.0f))
			graphicsSystem->setShadowColor((float3)shadowColor, shadowColor.getW());
		if (ImGui::SliderFloat("Alpha", &shadowColor.floats.w, 0.0f, 1.0f))
			graphicsSystem->setShadowColor((float3)shadowColor, shadowColor.getW());

		ImGui::DragFloat("Distance", &csmSystem->distance, 1.0f);
		ImGui::DragFloat("Bias Constant Factor", &csmSystem->biasConstantFactor, 0.01f);
		ImGui::DragFloat("Bias Slope Factor", &csmSystem->biasSlopeFactor, 0.01f);
		ImGui::DragFloat("Bias Normal Factor", &csmSystem->biasNormalFactor, 0.01f);
		ImGui::DragFloat("Z-Axis Offset Coefficient", &csmSystem->zCoeff, 0.01f);
		ImGui::SliderFloat2("Cascade Splits", &csmSystem->cascadeSplits, 0.001f, 0.999f);

		ImGui::Checkbox("Visualize Cascades", &visualizeCascades);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Cascade 1 = green, 2 = yellow, 3 = red, outside = magenta");
			ImGui::EndTooltip();
		}

		ImGui::Text("Cascade Far Planes: %f, %f, %f", csmSystem->cascadeSplits.x * csmSystem->distance, 
			csmSystem->cascadeSplits.y * csmSystem->distance, csmSystem->distance);

		// TODO: set shadow map size, also set it in the settings

		if (visualizeCascades)
		{
			if (!cascadesPipeline)
			{
				auto deferredSystem = DeferredRenderSystem::Instance::get();
				ResourceSystem::GraphicsOptions options;
				cascadesPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/shadow-cascades", deferredSystem->getUiFramebuffer(), options);
			}

			auto pipelineView = graphicsSystem->get(cascadesPipeline);
			if (pipelineView->isReady())
			{
				if (!cascadesDS)
				{
					auto uniforms = getCascadesUniforms();
					cascadesDS = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
					SET_RESOURCE_DEBUG_NAME(cascadesDS, "descriptorSet.editor.csm.cascades");
				}
			}
			else
			{
				ImGui::TextDisabled("Cascades pipeline is loading...");
			}
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

	auto csmSystem = CsmRenderSystem::Instance::get();
	const auto& cameraConstants = graphicsSystem->getCameraConstants();

	PushConstants pc;
	pc.farPlanes = (float3)(cameraConstants.nearPlane / csmSystem->getFarPlanes());

	SET_GPU_DEBUG_LABEL("Shadow Map Cascades", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(cascadesDS);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void CsmRenderEditorSystem::gBufferRecreate()
{
	if (cascadesDS)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cascadesDS);
		auto uniforms = getCascadesUniforms();
		cascadesDS = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cascadesDS, "descriptorSet.editor.csm.cascades");
	}
}

void CsmRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Cascade Shadow Mapping (CSM)"))
		showWindow = true;
}
#endif