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

static map<string, DescriptorSet::Uniform> getCascadesUniforms()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());		
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "depthBuffer", DescriptorSet::Uniform(gFramebufferView->getDepthStencilAttachment().imageView) } };
	return uniforms;
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
		graphicsSystem->destroy(cascadesDescriptorSet);
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
		const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
		auto shadowColor = cameraConstants.shadowColor;
		if (ImGui::SliderFloat3("Color", &shadowColor, 0.0f, 1.0f))
			graphicsSystem->setShadowColor(shadowColor);
		if (ImGui::SliderFloat("Alpha", &shadowColor.floats.w, 0.0f, 1.0f))
			graphicsSystem->setShadowColor(shadowColor);

		ImGui::DragFloat("Distance", &csmSystem->distance, 1.0f);
		ImGui::DragFloat("Constant Factor", &csmSystem->biasConstantFactor, 0.001f);
		ImGui::DragFloat("Slope Factor", &csmSystem->biasSlopeFactor, 0.0001f);
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
				cascadesPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
					"editor/shadow-cascades", deferredSystem->getUiFramebuffer());
			}

			auto pipelineView = graphicsSystem->get(cascadesPipeline);
			if (pipelineView->isReady())
			{
				if (!cascadesDescriptorSet)
				{
					auto uniforms = getCascadesUniforms();
					cascadesDescriptorSet = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
					SET_RESOURCE_DEBUG_NAME(cascadesDescriptorSet, "descriptorSet.editor.csm.cascades");
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
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto framebufferView = graphicsSystem->get(deferredSystem->getUiFramebuffer());
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->farPlanes = (float4)f32x4(cameraConstants.nearPlane / csmSystem->getFarPlanes(), 0.0f);

	SET_GPU_DEBUG_LABEL("Shadow Map Cascades", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(cascadesDescriptorSet);
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void CsmRenderEditorSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize && cascadesDescriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cascadesDescriptorSet);
		auto uniforms = getCascadesUniforms();
		cascadesDescriptorSet = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cascadesDescriptorSet, "descriptorSet.editor.csm.cascades");
	}
}

void CsmRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Cascade Shadow Mapping (CSM)"))
		showWindow = true;
}
#endif