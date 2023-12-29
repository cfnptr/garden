//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/graphics/editor/deferred.hpp"
#include "garden/system/graphics/lighting.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"
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

//--------------------------------------------------------------------------------------------------
static ID<Framebuffer> createEditorFramebuffer(DeferredRenderSystem* deferredSystem)
{
	auto graphicsSystem = deferredSystem->getGraphicsSystem();
	auto ldrFramebufferView = graphicsSystem->get(deferredSystem->getLdrFramebuffer());
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{
		Framebuffer::OutputAttachment(
			ldrFramebufferView->getColorAttachments()[0].imageView, false, true, true)
	};
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	Framebuffer::OutputAttachment depthStencilAttachment(
		gFramebufferView->getDepthStencilAttachment().imageView, false, true, true);
	auto framebuffer = graphicsSystem->createFramebuffer(
		deferredSystem->getFramebufferSize(),
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer, "framebuffer.deferred.editor");
	return framebuffer;
}

//--------------------------------------------------------------------------------------------------
static ID<Image> shadowPlaceholder = {};

static map<string, DescriptorSet::Uniform> getBufferUniforms(
	Manager* manager, GraphicsSystem* graphicsSystem,
	ID<Framebuffer> gFramebuffer, ID<Framebuffer> hdrFramebuffer)
{
	auto gFramebufferView = graphicsSystem->get(gFramebuffer);
	auto hdrFramebufferView = graphicsSystem->get(hdrFramebuffer);
	auto& colorAttachments = gFramebufferView->getColorAttachments();
	auto depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();
	auto lightingSystem = manager->tryGet<LightingRenderSystem>();

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
				Image::Bind::Sampled, { { nullptr } }, int2(1), Image::Strategy::Size);
			SET_RESOURCE_DEBUG_NAME(graphicsSystem, shadowPlaceholder,
				"image.shadowPlaceholder");
		}
		auto imageView = graphicsSystem->get(shadowPlaceholder);
		shadowBuffer0 = aoBuffer0 = aoBuffer1 = imageView->getDefaultView();
	}

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBuffer0", DescriptorSet::Uniform(colorAttachments[0].imageView) },
		{ "gBuffer1", DescriptorSet::Uniform(colorAttachments[1].imageView) },
		{ "gBuffer2", DescriptorSet::Uniform(colorAttachments[2].imageView) },
		{ "hdrBuffer", DescriptorSet::Uniform(
			hdrFramebufferView->getColorAttachments()[0].imageView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer0", DescriptorSet::Uniform(shadowBuffer0) },
		{ "aoBuffer0", DescriptorSet::Uniform(aoBuffer0) },
		{ "aoBuffer1", DescriptorSet::Uniform(aoBuffer1) },
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
DeferredEditor::DeferredEditor(DeferredRenderSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void DeferredEditor::prepare()
{
	if (editorFramebuffer && system->renderScale == 1.0f)
	{
		auto graphicsSystem = system->getGraphicsSystem();
		auto ldrFramebufferView = graphicsSystem->get(system->getLdrFramebuffer());
		Framebuffer::OutputAttachment colorAttachment(
			ldrFramebufferView->getColorAttachments()[0].imageView, false, true, true);
		auto gFramebufferView = graphicsSystem->get(system->getGFramebuffer());
		Framebuffer::OutputAttachment depthStencilAttachment(
			gFramebufferView->getDepthStencilAttachment().imageView, false, true, true);
		auto framebufferView = graphicsSystem->get(editorFramebuffer);
		framebufferView->update(system->getFramebufferSize(),
			&colorAttachment, 1, depthStencilAttachment);
	}
}

//--------------------------------------------------------------------------------------------------
void DeferredEditor::render()
{
	auto graphicsSystem = system->getGraphicsSystem();
	if (showWindow)
	{
		if (ImGui::Begin("G-Buffer Visualizer", &showWindow,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			const auto modes =
				"Off\0HDR\0Base Color\0Metallic\0Roughness\0Reflectance\0"
				"Emissive\0Normal\0World Position\0Depth\0Lighting\0"
				"Shadow\0Ambient Occlusion\0Ambient Occlusion (D)\0\0";
			ImGui::Combo("Draw Mode", drawMode, modes);

			if (drawMode == DrawMode::Lighting)
			{
				ImGui::SeparatorText("Overrides");
				ImGui::ColorEdit3("Base Color", (float*)&baseColorOverride);
				ImGui::ColorEdit3("Emissive", (float*)&emissiveOverride,
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
			bufferPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
				"editor/gbuffer-data", graphicsSystem->getSwapchainFramebuffer());
		}

		auto pipelineView = graphicsSystem->get(bufferPipeline);
		if (pipelineView->isReady() && graphicsSystem->camera)
		{
			// Note: we are doing this to get latest buffers. (suboptimal)
			graphicsSystem->destroy(bufferDescriptorSet);
			auto uniforms = getBufferUniforms(system->getManager(),
				graphicsSystem, system->gFramebuffer, system->hdrFramebuffer);
			bufferDescriptorSet = graphicsSystem->createDescriptorSet(
				bufferPipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(graphicsSystem, bufferDescriptorSet,
				"descriptorSet.deferred.editor.buffer");

			auto framebufferView = graphicsSystem->get(
				graphicsSystem->getSwapchainFramebuffer());
			auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();

			SET_GPU_DEBUG_LABEL("G-Buffer Visualizer", Color::transparent);
			framebufferView->beginRenderPass(float4(0.0f));
			pipelineView->bind();
			pipelineView->setViewportScissor(float4(float2(0),
				graphicsSystem->getFramebufferSize()));
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
		}
	}
}

//--------------------------------------------------------------------------------------------------
void DeferredEditor::deferredRender()
{
	if (drawMode == DrawMode::Lighting)
	{
		auto graphicsSystem = system->getGraphicsSystem();
		if (!lightingPipeline)
		{
			lightingPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
				"editor/pbr-lighting", system->getGFramebuffer(),
				system->isRenderAsync(), true);
		}

		auto pipelineView = graphicsSystem->get(lightingPipeline);
		if (pipelineView->isReady())
		{
			SET_GPU_DEBUG_LABEL("Lighting Visualizer", Color::transparent);

			if (system->isRenderAsync())
			{
				pipelineView->bindAsync(0, 0);
				pipelineView->setViewportScissorAsync(
					float4(float2(0), system->getFramebufferSize()), 0);
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
				pipelineView->setViewportScissor(
					float4(float2(0), system->getFramebufferSize()));
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
}

//--------------------------------------------------------------------------------------------------
void DeferredEditor::recreateSwapchain(const IRenderSystem::SwapchainChanges& changes)
{
	if (changes.framebufferSize)
	{
		auto graphicsSystem = system->getGraphicsSystem();

		if (editorFramebuffer)
		{
			auto gFramebufferView = graphicsSystem->get(system->getGFramebuffer());
			Framebuffer::OutputAttachment colorAttachment({}, false, true, true);
			Framebuffer::OutputAttachment depthStencilAttachment(
				gFramebufferView->getDepthStencilAttachment().imageView, false, true, true);

			if (system->renderScale == 1.0f)
			{
				auto swapchainView = graphicsSystem->get(
					graphicsSystem->getSwapchainFramebuffer());
				colorAttachment.imageView =
					swapchainView->getColorAttachments()[0].imageView;
			}
			else
			{
				auto ldrFramebufferView = graphicsSystem->get(
					system->getLdrFramebuffer());
				colorAttachment.imageView =
					ldrFramebufferView->getColorAttachments()[0].imageView;
			}

			auto framebufferView = graphicsSystem->get(editorFramebuffer);
			framebufferView->update(system->getFramebufferSize(),
				&colorAttachment, 1, depthStencilAttachment);
		}

		if (bufferDescriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(bufferDescriptorSet);
			auto uniforms = getBufferUniforms(system->getManager(),
				graphicsSystem, system->gFramebuffer, system->hdrFramebuffer);
			descriptorSetView->recreate(std::move(uniforms));
		}
	}
}

void DeferredEditor::onBarTool()
{
	if (ImGui::MenuItem("G-Buffer Visualizer")) showWindow = true;
}

ID<Framebuffer> DeferredEditor::getFramebuffer()
{
	if (!editorFramebuffer)
		editorFramebuffer = createEditorFramebuffer(system);
	return editorFramebuffer;
}
#endif