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

#include "garden/editor/system/render/deferred.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/lighting.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct BufferPC final
	{
		float4x4 viewProjInv;
		int32 drawMode;
		float showChannelR;
		float showChannelG;
		float showChannelB;
	};
	struct LightingPC final
	{
		float4 baseColor;
		float4 emissive;
		float metallic;
		float roughness;
		float reflectance;
	};
}

//**********************************************************************************************************************
static map<string, DescriptorSet::Uniform> getBufferUniforms(ID<Framebuffer> gFramebuffer,
	ID<Framebuffer> hdrFramebuffer, ID<Image>& shadowPlaceholder)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(gFramebuffer);
	auto hdrFramebufferView = graphicsSystem->get(hdrFramebuffer);
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
	const auto& depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();
	
	auto lightingSystem = LightingRenderSystem::Instance::tryGet();
	ID<ImageView> shadowBuffer0, aoBuffer0, aoBuffer1;

	if (lightingSystem)
	{
		shadowBuffer0 = lightingSystem->getShadowImageViews()[0];
		aoBuffer0 = lightingSystem->getAoImageViews()[0];
		aoBuffer1 = lightingSystem->getAoImageViews()[1];
	}
	else
	{
		if (!shadowPlaceholder)
		{
			shadowPlaceholder = graphicsSystem->createImage(Image::Format::UnormR8,
				Image::Bind::Sampled, { { nullptr } }, uint2(1), Image::Strategy::Size);
			SET_RESOURCE_DEBUG_NAME(shadowPlaceholder, "image.shadowPlaceholder");
		}
		auto imageView = graphicsSystem->get(shadowPlaceholder);
		shadowBuffer0 = aoBuffer0 = aoBuffer1 = imageView->getDefaultView();
	}

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBuffer0", DescriptorSet::Uniform(colorAttachments[0].imageView) },
		{ "gBuffer1", DescriptorSet::Uniform(colorAttachments[1].imageView) },
		{ "gBuffer2", DescriptorSet::Uniform(colorAttachments[2].imageView) },
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer0", DescriptorSet::Uniform(shadowBuffer0) },
		{ "aoBuffer0", DescriptorSet::Uniform(aoBuffer0) },
		{ "aoBuffer1", DescriptorSet::Uniform(aoBuffer1) },
	};
	return uniforms;
}

//**********************************************************************************************************************
ID<Image> DeferredRenderEditorSystem::shadowPlaceholder = {};

DeferredRenderEditorSystem::DeferredRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", DeferredRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
}
DeferredRenderEditorSystem::~DeferredRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
	}
	
	shadowPlaceholder = {};
}

void DeferredRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", DeferredRenderEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("DeferredRender", DeferredRenderEditorSystem::deferredRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", DeferredRenderEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", DeferredRenderEditorSystem::editorBarTool);
}
void DeferredRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", DeferredRenderEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("DeferredRender", DeferredRenderEditorSystem::deferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", DeferredRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", DeferredRenderEditorSystem::editorBarTool);
	}
	
	shadowPlaceholder = {};
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::editorRender()
{
	if (!GraphicsSystem::Instance::get()->canRender())
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (showWindow)
	{
		if (ImGui::Begin("G-Buffer Visualizer", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const auto modes = "Off\0HDR\0Base Color\0Metallic\0Roughness\0Reflectance\0Emissive\0Normal\0"
				"World Position\0Depth\0Lighting\0Shadow\0Ambient Occlusion\0Ambient Occlusion (D)\0\0";
			ImGui::Combo("Draw Mode", &drawMode, modes);

			if (drawMode == DrawMode::Lighting)
			{
				ImGui::SeparatorText("Overrides");
				ImGui::ColorEdit3("Base Color", &baseColorOverride);
				ImGui::ColorEdit3("Emissive", &emissiveOverride,
					ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
				ImGui::SliderFloat("Metallic", &metallicOverride, 0.0f, 1.0f);
				ImGui::SliderFloat("Roughness", &roughnessOverride, 0.0f, 1.0f);
				ImGui::SliderFloat("Reflectance", &reflectanceOverride, 0.0f, 1.0f);
			}
			else if ((int)drawMode > (int)DrawMode::Off)
			{
				ImGui::SeparatorText("Channels");
				ImGui::Checkbox("<- R", &showChannelR); ImGui::SameLine();
				ImGui::Checkbox("<- G", &showChannelG); ImGui::SameLine();
				ImGui::Checkbox("<- B", &showChannelB);
			}

			if (bufferPipeline)
			{
				auto pipelineView = graphicsSystem->get(bufferPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("G-Buffer pipeline is loading...");
			}
			if (lightingPipeline)
			{
				auto pipelineView = graphicsSystem->get(lightingPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("Lighting pipeline is loading...");
			}
		}
		ImGui::End();
	}

	if ((int)drawMode > (int)DrawMode::Off && (int)drawMode != (int)DrawMode::Lighting)
	{
		if (!bufferPipeline)
		{
			bufferPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
				"editor/gbuffer-data", graphicsSystem->getSwapchainFramebuffer());
		}

		auto pipelineView = graphicsSystem->get(bufferPipeline);
		if (pipelineView->isReady() && graphicsSystem->camera)
		{
			// TODO: we are doing this to get latest buffers. (suboptimal)
			auto deferredSystem = DeferredRenderSystem::Instance::get();
			graphicsSystem->destroy(bufferDescriptorSet);
			auto uniforms = getBufferUniforms(deferredSystem->getGFramebuffer(),
				deferredSystem->getHdrFramebuffer(), shadowPlaceholder);
			bufferDescriptorSet = graphicsSystem->createDescriptorSet(bufferPipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(bufferDescriptorSet, "descriptorSet.deferred.editor.buffer");

			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();

			SET_GPU_DEBUG_LABEL("G-Buffer Visualizer", Color::transparent);
			graphicsSystem->startRecording(CommandBufferType::Frame);
			framebufferView->beginRenderPass(float4(0.0f));
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(bufferDescriptorSet);
			auto pushConstants = pipelineView->getPushConstants<BufferPC>();
			pushConstants->viewProjInv = cameraConstants.viewProjInv;
			pushConstants->drawMode = (int32)drawMode;
			pushConstants->showChannelR = showChannelR ? 1.0f : 0.0f;
			pushConstants->showChannelG = showChannelG ? 1.0f : 0.0f;
			pushConstants->showChannelB = showChannelB ? 1.0f : 0.0f;
			pipelineView->pushConstants();
			pipelineView->drawFullscreen();
			framebufferView->endRenderPass();
			graphicsSystem->stopRecording();
		}
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::deferredRender()
{
	if (drawMode != DrawMode::Lighting)
		return;

	if (!lightingPipeline)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		lightingPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline("editor/pbr-lighting", 
			deferredSystem->getGFramebuffer(), deferredSystem->useAsyncRecording(), true);
	}

	auto pipelineView = GraphicsSystem::Instance::get()->get(lightingPipeline);
	if (pipelineView->isReady())
	{
		SET_GPU_DEBUG_LABEL("Lighting Visualizer", Color::transparent);

		if (DeferredRenderSystem::Instance::get()->useAsyncRecording())
		{
			pipelineView->bindAsync(0, 0);
			pipelineView->setViewportScissorAsync(float4(0.0f), 0);
			auto pushConstants = pipelineView->getPushConstantsAsync<LightingPC>(0);
			pushConstants->baseColor = baseColorOverride;
			pushConstants->emissive = emissiveOverride;
			pushConstants->metallic = metallicOverride;
			pushConstants->roughness = roughnessOverride;
			pushConstants->reflectance = reflectanceOverride;
			pipelineView->pushConstantsAsync(0);
			pipelineView->drawFullscreenAsync(0);
			// TODO: also support translucent overrides.
		}
		else
		{
			pipelineView->bind();
			pipelineView->setViewportScissor();
			auto pushConstants = pipelineView->getPushConstants<LightingPC>();
			pushConstants->baseColor = baseColorOverride;
			pushConstants->emissive = emissiveOverride;
			pushConstants->metallic = metallicOverride;
			pushConstants->roughness = roughnessOverride;
			pushConstants->reflectance = reflectanceOverride;
			pipelineView->pushConstants();
			pipelineView->drawFullscreen();
		}
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::gBufferRecreate()
{
	if (bufferDescriptorSet)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		auto descriptorSetView = GraphicsSystem::Instance::get()->get(bufferDescriptorSet);
		auto uniforms = getBufferUniforms(deferredSystem->getGFramebuffer(),
			deferredSystem->getHdrFramebuffer(), shadowPlaceholder);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

void DeferredRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("G-Buffer Visualizer"))
		showWindow = true;
}
#endif