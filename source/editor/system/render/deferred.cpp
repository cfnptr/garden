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
	auto hdrBufferView = deferredSystem->getHdrImageView();
	auto oitAccumBufferView = deferredSystem->getOitAccumIV();
	auto oitRevealBufferView = deferredSystem->getOitRevealIV();
	auto disocclMapView = deferredSystem->getDisocclView(0);
	auto depthBufferView = deferredSystem->getDepthImageView();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	const auto& gColorAttachments = gFramebufferView->getColorAttachments();
	
	auto pbrLightingSystem = PbrLightingSystem::Instance::tryGet();
	ID<ImageView> shadBuffer, shadBlurBuffer, aoBuffer, aoBlurBuffer, reflBuffer, giBuffer;

	if (pbrLightingSystem)
	{
		shadBuffer = pbrLightingSystem->getShadBaseView();
		shadBlurBuffer = pbrLightingSystem->getShadBlurView();
		aoBuffer = pbrLightingSystem->getAoBaseView();
		aoBlurBuffer = pbrLightingSystem->getAoBlurView();
		reflBuffer = pbrLightingSystem->getReflBaseView();
		giBuffer = pbrLightingSystem->getGiBuffer() ? graphicsSystem->get(
			pbrLightingSystem->getGiBuffer())->getView() : graphicsSystem->getEmptyTexture();

		if (!shadBuffer)
			shadBuffer = shadBlurBuffer = graphicsSystem->getEmptyTexture();
		if (!aoBuffer)
			aoBuffer = aoBlurBuffer = graphicsSystem->getEmptyTexture();
		if (!reflBuffer)
			reflBuffer = graphicsSystem->getEmptyTexture();
	}
	else
	{
		auto emptyTexture = graphicsSystem->getEmptyTexture();
		shadBuffer = shadBlurBuffer = emptyTexture;
		aoBuffer = aoBlurBuffer = emptyTexture;
		reflBuffer = emptyTexture; giBuffer = emptyTexture;
	}

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "hdrBuffer", DescriptorSet::Uniform(hdrBufferView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "reflBuffer", DescriptorSet::Uniform(reflBuffer) },
		{ "shadBuffer", DescriptorSet::Uniform(shadBuffer) },
		{ "shadBlurBuffer", DescriptorSet::Uniform(shadBlurBuffer) },
		{ "aoBuffer", DescriptorSet::Uniform(aoBuffer) },
		{ "aoBlurBuffer", DescriptorSet::Uniform(aoBlurBuffer) },
		{ "giBuffer", DescriptorSet::Uniform(giBuffer) },
		{ "oitAccumBuffer", DescriptorSet::Uniform(oitAccumBufferView) },
		{ "oitRevealBuffer", DescriptorSet::Uniform(oitRevealBufferView) },
		{ "disocclMap", DescriptorSet::Uniform(disocclMapView) }
	};

	for (uint8 i = 0; i < G_BUFFER_COUNT; i++)
	{
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(gColorAttachments[i].imageView ? 
			gColorAttachments[i].imageView : graphicsSystem->getEmptyTexture()));
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
		ResourceSystem::GraphicsOptions options;
		options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;
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
			auto isSpecular = bool(lightingPC.materialID & G_MATERIAL_SPECULAR);
			auto isEmissive = bool(lightingPC.materialID & G_MATERIAL_EMISSIVE);
			auto isClearCoat = bool(lightingPC.materialID & G_MATERIAL_CLEAR_COAT);
			auto isSubsurface = bool(lightingPC.materialID & G_MATERIAL_SUBSURFACE);
			auto isSheen = bool(lightingPC.materialID & G_MATERIAL_SHEEN);

			ImGui::SeparatorText("Overrides");
			ImGui::Checkbox("Specular", &isSpecular); ImGui::SameLine();
			ImGui::Checkbox("Emissive", &isEmissive);
			ImGui::Checkbox("ClearCoat", &isClearCoat); ImGui::SameLine();
			ImGui::Checkbox("Subsurface", &isSubsurface);
			ImGui::Spacing();
			// TODO: sheen
			
			lightingPC.materialID = (isSpecular ? G_MATERIAL_SPECULAR : 0u) | 
				(isEmissive ? G_MATERIAL_EMISSIVE : 0u) | (isClearCoat ? G_MATERIAL_CLEAR_COAT : 0u) |
				(isSubsurface ? G_MATERIAL_SUBSURFACE : 0u) | (isSheen ? G_MATERIAL_SHEEN : 0u);

			ImGui::ColorEdit3("Base Color", &lightingPC.baseColor);
			ImGui::SliderFloat("Metallic", &lightingPC.mraor.x, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness", &lightingPC.mraor.y, 0.0f, 1.0f);
			ImGui::BeginDisabled(isEmissive || isClearCoat);
			ImGui::SliderFloat("Ambient Occlusion", &lightingPC.mraor.z, 0.0f, 1.0f);
			ImGui::EndDisabled();
			ImGui::SliderFloat("Reflectance", &lightingPC.mraor.w, 0.0f, 1.0f);
			ImGui::SliderFloat("Shadow Alpha", &lightingPC.shadowAlpha, 0.0f, 1.0f);

			ImGui::BeginDisabled(!isSpecular);
			ImGui::SliderFloat("Specular Factor", &lightingPC.specularFactor, 0.0f, 1.0f);
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!isEmissive);
			ImGui::SliderFloat("Emissive Factor", &lightingPC.emissiveFactor, 0.0f, 1.0f);
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!isClearCoat);
			ImGui::SliderFloat("Clear Coat", &lightingPC.clearCoat, 0.0f, 1.0f);
			ImGui::SliderFloat("Clear Coat Roughness", &lightingPC.clearCoatRoughness, 0.0f, 1.0f);
			ImGui::EndDisabled();

			// TODO: allow to select custom passed emissive or subsurface color via buffer to the pbr lighting shader.
		}
		else if (drawMode == G_BUFFER_DRAW_MODE_VELOCITY && !deferredSystem->getOptions().useVelocity)
		{
			ImGui::TextDisabled("Velocity buffer is disabled in deferred system!");
		}
		else if (drawMode == G_BUFFER_DRAW_MODE_DISOCCLUSION && !deferredSystem->getOptions().useDisoccl)
		{
			ImGui::TextDisabled("Disocclusion buffer is disabled in deferred system!");
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
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if ((drawMode == G_BUFFER_DRAW_MODE_VELOCITY && !deferredSystem->getOptions().useVelocity) ||
		(drawMode == G_BUFFER_DRAW_MODE_DISOCCLUSION && !deferredSystem->getOptions().useDisoccl))
	{
		drawMode = G_BUFFER_DRAW_MODE_OFF;
	}

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
	pc.showChannelR = showChannelR ? 1.0f : 0.0f;
	pc.showChannelG = showChannelG ? 1.0f : 0.0f;
	pc.showChannelB = showChannelB ? 1.0f : 0.0f;

	SET_GPU_DEBUG_LABEL("G-Buffer Visualizer");
	pipelineView->bind(drawMode);
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(bufferDescriptorSet);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void DeferredRenderEditorSystem::gBufferRecreate()
{
	GraphicsSystem::Instance::get()->destroy(bufferDescriptorSet);
}

void DeferredRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("G-Buffer Visualizer"))
		showWindow = true;
}
#endif