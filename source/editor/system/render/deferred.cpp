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

#include "garden/editor/system/render/deferred.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
static void getBlackPlaceholder(ID<Image>& blackPlaceholder)
{
	if (!blackPlaceholder)
	{
		blackPlaceholder = GraphicsSystem::Instance::get()->createImage(Image::Format::UnormR8,
			Image::Bind::Sampled, { { nullptr } }, uint2::one, Image::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(blackPlaceholder, "image.editor.deferred.blackPlaceholder");
	}
}

static map<string, DescriptorSet::Uniform> getBufferUniforms(ID<Image>& blackPlaceholder)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
	const auto& depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();
	
	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::tryGet();
	ID<ImageView> shadowBuffer0, aoBuffer0, aoBuffer1;

	if (pbrLightingSystem)
	{
		shadowBuffer0 = pbrLightingSystem->getShadowImageViews()[0];
		aoBuffer0 = pbrLightingSystem->getAoImageViews()[0];
		aoBuffer1 = pbrLightingSystem->getAoImageViews()[1];
	}
	else
	{
		getBlackPlaceholder(blackPlaceholder);
		auto imageView = graphicsSystem->get(blackPlaceholder);
		aoBuffer0 = aoBuffer1 = imageView->getDefaultView();
	}

	if (!shadowBuffer0)
	{
		getBlackPlaceholder(blackPlaceholder);
		auto imageView = graphicsSystem->get(blackPlaceholder);
		shadowBuffer0 = imageView->getDefaultView();
	}

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer0", DescriptorSet::Uniform(shadowBuffer0) },
		{ "aoBuffer0", DescriptorSet::Uniform(aoBuffer0) },
		{ "aoBuffer1", DescriptorSet::Uniform(aoBuffer1) },
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(colorAttachments[i].imageView));

	return uniforms;
}

//**********************************************************************************************************************
DeferredRenderEditorSystem::DeferredRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", DeferredRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
}
DeferredRenderEditorSystem::~DeferredRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
	}
}

void DeferredRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", DeferredRenderEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("DeferredRender", DeferredRenderEditorSystem::deferredRender);
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", DeferredRenderEditorSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("LdrRender", DeferredRenderEditorSystem::ldrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", DeferredRenderEditorSystem::gBufferRecreate);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", DeferredRenderEditorSystem::editorBarTool);
}
void DeferredRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(bufferDescriptorSet);
		graphicsSystem->destroy(pbrLightingPipeline);
		graphicsSystem->destroy(bufferPipeline);
		graphicsSystem->destroy(blackPlaceholder);

		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", DeferredRenderEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("DeferredRender", DeferredRenderEditorSystem::deferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", DeferredRenderEditorSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("LdrRender", DeferredRenderEditorSystem::ldrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", DeferredRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", DeferredRenderEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::editorRender()
{
	if (!GraphicsSystem::Instance::get()->canRender())
		return;
	
	if (showWindow)
	{
		if (ImGui::Begin("G-Buffer Visualizer", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			constexpr auto modes = "Off\0Base Color\0Opacity / Transmission\0Metallic\0Roughness\0Material AO\0"
				"Reflectance\0Normals\0Clear Coat\0Emissive Color\0Emissive Factor\0Subsurface Color\0Thickness\0"
				"Lighting\0HDR\0Depth\0World Position\0Shadows\0Global AO\0Denoised Global AO\0\0";
			ImGui::Combo("Draw Mode", &drawMode, modes);

			if (drawMode == DrawMode::Lighting)
			{
				ImGui::SeparatorText("Overrides");
				ImGui::ColorEdit3("Base Color", &colorOverride);
				ImGui::SliderFloat("Opaque / Transmission", &colorOverride.floats.w, 0.0f, 1.0f);
				ImGui::SliderFloat("Metallic", &mraorOverride.floats.x, 0.0f, 1.0f);
				ImGui::SliderFloat("Roughness", &mraorOverride.floats.y, 0.0f, 1.0f);
				ImGui::SliderFloat("Ambient Occlusion", &mraorOverride.floats.z, 0.0f, 1.0f);
				ImGui::SliderFloat("Reflectance", &mraorOverride.floats.w, 0.0f, 1.0f);
				ImGui::ColorEdit3("Emissive Color", &emissiveOverride);
				ImGui::SliderFloat("Emissive Factor", &emissiveOverride.floats.w, 0.0f, 1.0f);
				ImGui::ColorEdit3("Subsurface Color", &subsurfaceOverride);
				ImGui::SliderFloat("Thickness", &subsurfaceOverride.floats.w, 0.0f, 1.0f);
				ImGui::SliderFloat("Clear Coat", &clearCoatOverride, 0.0f, 1.0f);
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
				auto pipelineView = GraphicsSystem::Instance::get()->get(bufferPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("G-Buffer pipeline is loading...");
			}
			if (pbrLightingPipeline)
			{
				auto pipelineView = GraphicsSystem::Instance::get()->get(pbrLightingPipeline);
				if (!pipelineView->isReady())
					ImGui::TextDisabled("PBR lighting pipeline is loading...");
			}
		}
		ImGui::End();
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::deferredRender()
{
	if (drawMode != DrawMode::Lighting)
		return;

	if (!pbrLightingPipeline)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		pbrLightingPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline("editor/pbr-lighting",
			deferredSystem->getGFramebuffer(), deferredSystem->useAsyncRecording());
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pbrLightingPipeline);

	if (pipelineView->isReady())
	{
		auto threadIndex = graphicsSystem->getThreadCount() - 1;
		auto pushConstants = pipelineView->getPushConstants<LightingPC>(threadIndex);
		pushConstants->color = (float4)colorOverride;
		pushConstants->mraor = (float4)mraorOverride;
		pushConstants->emissive = (float4)emissiveOverride;
		pushConstants->subsurface = (float4)emissiveOverride;
		pushConstants->clearCoat = clearCoatOverride;

		SET_GPU_DEBUG_LABEL("PBR Lighting Visualizer", Color::transparent);
		if (graphicsSystem->isCurrentRenderPassAsync())
		{
			pipelineView->bindAsync(0, threadIndex);
			pipelineView->setViewportScissorAsync(f32x4::zero, threadIndex);
			pipelineView->pushConstantsAsync(threadIndex);
			pipelineView->drawFullscreenAsync(threadIndex);
			
		}
		else
		{
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->pushConstants();
			pipelineView->drawFullscreen();
			// TODO: also support translucent overrides.
		}
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::preLdrRender()
{
	if ((int)drawMode == (int)DrawMode::Off || (int)drawMode == (int)DrawMode::Lighting)
		return;

	if (!bufferPipeline)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		bufferPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/gbuffer-data", deferredSystem->getLdrFramebuffer());
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(bufferPipeline);

	if (pipelineView->isReady())
	{
		if (!bufferDescriptorSet)
		{
			auto uniforms = getBufferUniforms(blackPlaceholder);
			bufferDescriptorSet = graphicsSystem->createDescriptorSet(bufferPipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(bufferDescriptorSet, "descriptorSet.deferred.editor.buffer");
		}
	}
}
void DeferredRenderEditorSystem::ldrRender()
{
	if ((int)drawMode == (int)DrawMode::Off || (int)drawMode == (int)DrawMode::Lighting)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(bufferPipeline);

	if (pipelineView->isReady() && graphicsSystem->camera)
	{
		const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
		auto pushConstants = pipelineView->getPushConstants<BufferPC>();
		pushConstants->invViewProj = (float4x4)cameraConstants.invViewProj;
		pushConstants->drawMode = (int32)drawMode;
		pushConstants->showChannelR = showChannelR ? 1.0f : 0.0f;
		pushConstants->showChannelG = showChannelG ? 1.0f : 0.0f;
		pushConstants->showChannelB = showChannelB ? 1.0f : 0.0f;

		SET_GPU_DEBUG_LABEL("G-Buffer Visualizer", Color::transparent);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(bufferDescriptorSet);
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::gBufferRecreate()
{
	if (bufferDescriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(bufferDescriptorSet);
		auto uniforms = getBufferUniforms(blackPlaceholder);
		bufferDescriptorSet = graphicsSystem->createDescriptorSet(bufferPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(bufferDescriptorSet, "descriptorSet.deferred.editor.buffer");
	}
}

void DeferredRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("G-Buffer Visualizer"))
		showWindow = true;
}
#endif