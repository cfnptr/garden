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
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", CsmRenderEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", CsmRenderEditorSystem::editorBarTool);
}
void CsmRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cascadesDescriptorSet);
		graphicsSystem->destroy(cascadesPipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", CsmRenderEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", CsmRenderEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void CsmRenderEditorSystem::editorRender()
{
	if (!GraphicsSystem::Instance::get()->canRender())
		return;

	if (showWindow)
	{
		auto csmSystem = CsmRenderSystem::Instance::get();
		if (ImGui::Begin("Cascade Shadow Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::SliderFloat("Intensity", &csmSystem->intensity, 0.0f, 1.0f);
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

			auto graphicsSystem = GraphicsSystem::Instance::get();
			const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();

			ImGui::Text("Cascade Far Planes: %f, %f, %f",
				csmSystem->cascadeSplits.x * csmSystem->distance + cameraConstants.nearPlane,
				csmSystem->cascadeSplits.y * csmSystem->distance + cameraConstants.nearPlane,
				csmSystem->distance + cameraConstants.nearPlane);

			// TODO: set shadow map size, also set it in the settings

			if (cascadesPipeline)
			{
				auto pipelineView = GraphicsSystem::Instance::get()->get(cascadesPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("Cascades pipeline is loading...");
			}
		}
		ImGui::End();
	}

	if (visualizeCascades)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		if (!cascadesPipeline)
		{
			cascadesPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
				"editor/shadow-cascades", graphicsSystem->getSwapchainFramebuffer());
		}
		
		auto pipelineView = graphicsSystem->get(cascadesPipeline);
		if (pipelineView->isReady())
		{
			if (!cascadesDescriptorSet)
			{
				auto uniforms = getCascadesUniforms();
				cascadesDescriptorSet = graphicsSystem->createDescriptorSet(cascadesPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(cascadesDescriptorSet, "descriptorSet.csm.editor.cascades");
			}

			auto csmSystem = CsmRenderSystem::Instance::get();
			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
			auto pushConstants = pipelineView->getPushConstants<PushConstants>();
			pushConstants->farPlanes = float4(cameraConstants.nearPlane / csmSystem->getFarPlanes(), 0.0f);

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Shadow Map Cascades", Color::transparent);
				framebufferView->beginRenderPass(float4(0.0f));
				pipelineView->bind();
				pipelineView->setViewportScissor();
				pipelineView->bindDescriptorSet(cascadesDescriptorSet);
				pipelineView->pushConstants();
				pipelineView->drawFullscreen();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
}

void CsmRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Cascade Shadow Mapping (CSM)"))
		showWindow = true;
}
#endif