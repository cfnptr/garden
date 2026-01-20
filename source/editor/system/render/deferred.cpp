// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
static DescriptorSet::Uniforms getBufferUniforms(GraphicsSystem* graphicsSystem, ID<Image>& blackPlaceholder)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer());
	auto oitFramebufferView = graphicsSystem->get(deferredSystem->getOitFramebuffer());
	auto hdrBufferView = hdrFramebufferView->getColorAttachments()[0].imageView;
	auto ldrBufferView = ldrFramebufferView->getColorAttachments()[0].imageView;
	auto oitAccumBufferView = oitFramebufferView->getColorAttachments()[0].imageView;
	auto oitRevealBufferView = oitFramebufferView->getColorAttachments()[1].imageView;
	auto depthBufferView = deferredSystem->getDepthImageView();
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
	
	auto pbrLightingSystem = PbrLightingSystem::Instance::tryGet();
	ID<ImageView> shadowBuffer, shadowBlurBuffer, aoBuffer, aoBlurBuffer, reflBuffer, giBuffer;

	if (pbrLightingSystem)
	{
		shadowBuffer = pbrLightingSystem->getShadowBaseView();
		shadowBlurBuffer = pbrLightingSystem->getShadowTempView();
		aoBuffer = pbrLightingSystem->getAoBaseView();
		aoBlurBuffer = pbrLightingSystem->getAoBlurView();
		reflBuffer = pbrLightingSystem->getReflBaseView();
		giBuffer = pbrLightingSystem->getGiBuffer() ? graphicsSystem->get(
			pbrLightingSystem->getGiBuffer())->getDefaultView() : graphicsSystem->getEmptyTexture();

		if (!shadowBuffer)
			shadowBuffer = shadowBlurBuffer = graphicsSystem->getEmptyTexture();
		if (!aoBuffer)
			aoBuffer = aoBlurBuffer = graphicsSystem->getEmptyTexture();
		if (!reflBuffer)
			reflBuffer = graphicsSystem->getEmptyTexture();
	}
	else
	{
		auto emptyTexture = graphicsSystem->getEmptyTexture();
		shadowBuffer = shadowBlurBuffer = emptyTexture;
		aoBuffer = aoBlurBuffer = emptyTexture;
		reflBuffer = emptyTexture; giBuffer = emptyTexture;
	}

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "ldrBuffer", DescriptorSet::Uniform(ldrBufferView) },
		{ "oitAccumBuffer", DescriptorSet::Uniform(oitAccumBufferView) },
		{ "oitRevealBuffer", DescriptorSet::Uniform(oitRevealBufferView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "shadowBuffer", DescriptorSet::Uniform(shadowBuffer) },
		{ "shadowBlurBuffer", DescriptorSet::Uniform(shadowBlurBuffer) },
		{ "aoBuffer", DescriptorSet::Uniform(aoBuffer) },
		{ "aoBlurBuffer", DescriptorSet::Uniform(aoBlurBuffer) },
		{ "reflBuffer", DescriptorSet::Uniform(reflBuffer) },
		{ "giBuffer", DescriptorSet::Uniform(giBuffer) }
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(colorAttachments[i].imageView ? 
			colorAttachments[i].imageView : graphicsSystem->getEmptyTexture()));
	}

	return uniforms;
}

//**********************************************************************************************************************
DeferredRenderEditorSystem::DeferredRenderEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", DeferredRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
}
DeferredRenderEditorSystem::~DeferredRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderEditorSystem::deinit);
	}
}

void DeferredRenderEditorSystem::init()
{
	auto manager = Manager::Instance::get();
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

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("DeferredRender", DeferredRenderEditorSystem::deferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", DeferredRenderEditorSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("LdrRender", DeferredRenderEditorSystem::ldrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", DeferredRenderEditorSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", DeferredRenderEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::deferredRender()
{
	if (drawMode != G_BUFFER_DRAW_MODE_LIGHTING_DEBUG)
		return;

	if (!pbrLightingPipeline)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		Pipeline::SpecConstValues specConstValues =
		{
			{ "USE_CLEAR_COAT_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useClearCoat) },
			{ "USE_EMISSION_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useEmission) }
		};

		ResourceSystem::GraphicsOptions options;
		options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
		options.specConstValues = &specConstValues;

		pbrLightingPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/pbr-lighting", deferredSystem->getGFramebuffer(), options);
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pbrLightingPipeline);

	if (!pipelineView->isReady())
		return;

	if (graphicsSystem->isCurrentRenderPassAsync())
	{
		SET_GPU_DEBUG_LABEL_ASYNC("PBR Lighting Visualizer", INT32_MAX);
		pipelineView->bindAsync(0, INT32_MAX);
		pipelineView->setViewportScissorAsync(float4::zero, INT32_MAX);
		pipelineView->pushConstantsAsync(&lightingPC, INT32_MAX);
		pipelineView->drawFullscreenAsync(INT32_MAX);
	}
	else
	{
		SET_GPU_DEBUG_LABEL("PBR Lighting Visualizer");
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->pushConstants(&lightingPC);
		pipelineView->drawFullscreen();
		// TODO: also support translucent overrides.
	}
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::preLdrRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("G-Buffer Visualizer", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Combo("Draw Mode", &drawMode, G_BUFFER_DRAW_MODE_NAMES, G_BUFFER_DRAW_MODE_COUNT);

		auto deferredSystem = DeferredRenderSystem::Instance::get();
		if (drawMode == G_BUFFER_DRAW_MODE_LIGHTING_DEBUG)
		{
			ImGui::SeparatorText("Overrides");
			ImGui::ColorEdit3("Base Color", &lightingPC.baseColor);
			ImGui::SliderFloat("Specular Factor", &lightingPC.specularFactor, 0.0f, 1.0f);
			ImGui::SliderFloat("Metallic", &lightingPC.mraor.x, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness", &lightingPC.mraor.y, 0.0f, 1.0f);
			ImGui::SliderFloat("Ambient Occlusion", &lightingPC.mraor.z, 0.0f, 1.0f);
			ImGui::SliderFloat("Reflectance", &lightingPC.mraor.w, 0.0f, 1.0f);
			ImGui::SliderFloat("G-Buffer Shadows", &lightingPC.shadow, 0.0f, 1.0f);

			ImGui::BeginDisabled(!deferredSystem->getOptions().useClearCoat);
			ImGui::SliderFloat("Clear Coat Roughness", &lightingPC.ccRoughness, 0.0f, 1.0f);
			ImGui::EndDisabled();

			ImGui::BeginDisabled(!deferredSystem->getOptions().useEmission);
			ImGui::ColorEdit3("Emissive Color", &lightingPC.emissiveColor);
			ImGui::SliderFloat("Emissive Factor", &lightingPC.emissiveFactor, 0.0f, 1.0f);
			ImGui::EndDisabled();
		}
		else if ((drawMode == G_BUFFER_DRAW_MODE_CC_NORMAL || 
			drawMode == G_BUFFER_DRAW_MODE_CC_ROUGHNESS) && !deferredSystem->getOptions().useClearCoat)
		{
			ImGui::TextDisabled("Clear coat buffer is disabled in deferred system!");
		}
		else if ((drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_COLOR || 
			drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR) && !deferredSystem->getOptions().useEmission)
		{
			ImGui::TextDisabled("Emission buffer is disabled in deferred system!");
		}
		else if (drawMode == G_BUFFER_DRAW_MODE_VELOCITY && !deferredSystem->getOptions().useVelocity)
		{
			ImGui::TextDisabled("Velocity buffer is disabled in deferred system!");
		}
		
		else if (drawMode > G_BUFFER_DRAW_MODE_OFF)
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

	if (drawMode == G_BUFFER_DRAW_MODE_OFF || drawMode == G_BUFFER_DRAW_MODE_LIGHTING_DEBUG)
		return;

	if (!bufferPipeline)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		ResourceSystem::GraphicsOptions options;
		bufferPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/gbuffer-data", deferredSystem->getLdrFramebuffer(), options);
	}
}
//**********************************************************************************************************************
void DeferredRenderEditorSystem::ldrRender()
{
	if (drawMode == G_BUFFER_DRAW_MODE_OFF || drawMode == G_BUFFER_DRAW_MODE_LIGHTING_DEBUG)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(bufferPipeline);
	if (!pipelineView->isReady())
		return;

	if (!bufferDescriptorSet)
	{
		auto uniforms = getBufferUniforms(graphicsSystem, blackPlaceholder);
		bufferDescriptorSet = graphicsSystem->createDescriptorSet(bufferPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(bufferDescriptorSet, "descriptorSet.editor.deferred.buffer");
	}

	const auto& cc = graphicsSystem->getCommonConstants();

	BufferPC pc;
	pc.invViewProj = (float4x4)cc.invViewProj;
	pc.drawMode = (int32)drawMode;
	pc.showChannelR = showChannelR ? 1.0f : 0.0f;
	pc.showChannelG = showChannelG ? 1.0f : 0.0f;
	pc.showChannelB = showChannelB ? 1.0f : 0.0f;

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (((drawMode == G_BUFFER_DRAW_MODE_CC_NORMAL || drawMode == 
			G_BUFFER_DRAW_MODE_CC_ROUGHNESS) && !deferredSystem->getOptions().useClearCoat) || 
		((drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_COLOR || drawMode == 
			G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR) && !deferredSystem->getOptions().useEmission) ||
		(drawMode == G_BUFFER_DRAW_MODE_VELOCITY && !deferredSystem->getOptions().useVelocity))
	{
		pc.drawMode = G_BUFFER_DRAW_MODE_OFF;
	}

	SET_GPU_DEBUG_LABEL("G-Buffer Visualizer");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(bufferDescriptorSet);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::gBufferRecreate()
{
	if (bufferDescriptorSet)
	{
		GraphicsSystem::Instance::get()->destroy(bufferDescriptorSet);
		bufferDescriptorSet = {};
	}
}

void DeferredRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("G-Buffer Visualizer"))
		showWindow = true;
}
#endif