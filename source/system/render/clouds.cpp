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

#include "garden/system/render/clouds.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;

static ID<Image> createCloudsView(GraphicsSystem* graphicsSystem)
{
	auto size = max(graphicsSystem->getScaledFrameSize() / 2, uint2::one);
	auto cloudsView = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, size, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.view");
	return cloudsView;
}
static ID<Image> createCloudsCube(GraphicsSystem* graphicsSystem, uint32 skyboxSize)
{
	auto size = max(skyboxSize / 2, 1u);
	auto cloudsView = graphicsSystem->createImage(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | 
		Image::Usage::ColorAttachment, { { nullptr } }, uint2(size), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(cloudsView, "image.clouds.cube");
	return cloudsView;
}

static ID<Framebuffer> createFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> cloudsBuffer)
{
	auto cloudsBufferView = graphicsSystem->get(cloudsBuffer);
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(cloudsBufferView->getDefaultView(), CloudsRenderSystem::framebufferFlags) };

	auto framebuffer = graphicsSystem->createFramebuffer(
		(uint2)cloudsBufferView->getSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.clouds");
	return framebuffer;
}

static Ref<Image> createNoiseShape()
{
	return ResourceSystem::Instance::get()->loadImage(
		"clouds/noise-shape", Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ, 1, 
		Image::Strategy::Size, ImageLoadFlags::LoadShared | ImageLoadFlags::Load3D | ImageLoadFlags::LinearData, 9.0f);
}
static Ref<Image> createNoiseErosion()
{
	return ResourceSystem::Instance::get()->loadImage(
		"clouds/noise-erosion", Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ, 1, 
		Image::Strategy::Size, ImageLoadFlags::LoadShared | ImageLoadFlags::Load3D | ImageLoadFlags::LinearData, 9.0f);
}
static ID<GraphicsPipeline> createCloudsPipeline(ID<Framebuffer> framebuffer, GraphicsQuality quality)
{
	float sampleCount;
	switch (quality)
	{
		// TODO: case GraphicsQuality::Ultra: sampleCount = 30.0f; break;
		default: sampleCount = 0.1f; break;
	}
	Pipeline::SpecConstValues specConstValues = { { "SAMPLE_COUNT", Pipeline::SpecConstValue(sampleCount) } };
	
	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("atmosphere/volumetric-clouds", framebuffer, options);
}
static ID<GraphicsPipeline> createBlendPipeline()
{
	auto hdrFramebuffer = DeferredRenderSystem::Instance::get()->getHdrFramebuffer();
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/alpha-blend", hdrFramebuffer, options);
}

static DescriptorSet::Uniforms getCloudsUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> noiseShape, ID<Image> noiseErosion)
{
	auto noiseShapeView = graphicsSystem->get(noiseShape)->getDefaultView();
	auto noiseErosionView = graphicsSystem->get(noiseErosion)->getDefaultView();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "noiseShape", DescriptorSet::Uniform(noiseShapeView) },
		{ "noiseErosion", DescriptorSet::Uniform(noiseErosionView) }
	};
	return uniforms;
}
static DescriptorSet::Uniforms getBlendUniforms(GraphicsSystem* graphicsSystem, 
	ID<Image> cloudsBuffer)
{
	auto cloudsBufferView = graphicsSystem->get(cloudsBuffer)->getDefaultView();
	return { { "srcBuffer", DescriptorSet::Uniform(cloudsBufferView) } };
}

//**********************************************************************************************************************
CloudsRenderSystem::CloudsRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", CloudsRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CloudsRenderSystem::deinit);
}
CloudsRenderSystem::~CloudsRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CloudsRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CloudsRenderSystem::deinit);
	}

	unsetSingleton();
}

void CloudsRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
}
void CloudsRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(cubeBlendDS);
		graphicsSystem->destroy(viewBlendDS);
		graphicsSystem->destroy(cloudsDS);
		graphicsSystem->destroy(cubeFramebuffer);
		graphicsSystem->destroy(viewFramebuffer);
		graphicsSystem->destroy(cloudsCube);
		graphicsSystem->destroy(cloudsView);
		graphicsSystem->destroy(noiseErosion);
		graphicsSystem->destroy(noiseShape);
		graphicsSystem->destroy(blendPipeline);
		graphicsSystem->destroy(cloudsPipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", CloudsRenderSystem::preDeferredRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", CloudsRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", CloudsRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CloudsRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void CloudsRenderSystem::preDeferredRender()
{
	SET_CPU_ZONE_SCOPED("Clouds Pre Deferred Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isInitialized)
	{
		if (!noiseShape)
			noiseShape = createNoiseShape();
		if (!noiseErosion)
			noiseErosion = createNoiseErosion();
		if (!cloudsView)
			cloudsView = createCloudsView(graphicsSystem);
		if (!cloudsCube)
			cloudsCube = createCloudsView(graphicsSystem);
		if (!viewFramebuffer)
			viewFramebuffer = createFramebuffer(graphicsSystem, cloudsView);
		if (!cubeFramebuffer)
			cubeFramebuffer = createFramebuffer(graphicsSystem, cloudsCube);
		if (!cloudsPipeline)
			cloudsPipeline = createCloudsPipeline(viewFramebuffer, quality);
		if (!blendPipeline)
			blendPipeline = createBlendPipeline();
		isInitialized = true;
	}

	auto pipelineView = graphicsSystem->get(cloudsPipeline);
	auto noiseShapeView = graphicsSystem->get(noiseShape);
	auto noiseErosionView = graphicsSystem->get(noiseErosion);
	if (!pipelineView->isReady() || !noiseShapeView->isReady() || !noiseErosionView->isReady())
		return;

	if (!cloudsDS)
	{
		auto uniforms = getCloudsUniforms(graphicsSystem, ID<Image>(noiseShape), ID<Image>(noiseErosion));
		cloudsDS = graphicsSystem->createDescriptorSet(cloudsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cloudsDS, "descriptorSet.clouds");
	}

	auto framebufferView = graphicsSystem->get(cubeFramebuffer);
	pipelineView->updateFramebuffer(cubeFramebuffer);

	/* TODO:
	PushConstants pc;
	pc.invFrameSize = float2::one / framebufferView->getSize();

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Cube Clouds");
		{
			RenderPass renderPass(cubeFramebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(descriptorSet);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
	*/
}

//**********************************************************************************************************************
void CloudsRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(cloudsPipeline);
	auto noiseShapeView = graphicsSystem->get(noiseShape);
	auto noiseErosionView = graphicsSystem->get(noiseErosion);
	if (!pipelineView->isReady() || !noiseShapeView->isReady() || !noiseErosionView->isReady())
		return;

	if (!cloudsDS)
	{
		auto uniforms = getCloudsUniforms(graphicsSystem, ID<Image>(noiseShape), ID<Image>(noiseErosion));
		cloudsDS = graphicsSystem->createDescriptorSet(cloudsPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(cloudsDS, "descriptorSet.clouds");
	}

	auto& cc = graphicsSystem->getCommonConstants();
	auto cameraHeight =  max(cc.cameraPos.y, 0.0f) * 0.001f;
	auto framebufferView = graphicsSystem->get(viewFramebuffer);
	pipelineView->updateFramebuffer(viewFramebuffer);

	PushConstants pc;
	pc.invViewProj = (float4x4)cc.invViewProj;
	pc.cameraPos = float3(0.0f, cameraHeight, 0.0f);
	pc.bottomRadius = bottomRadius;
	pc.topRadius = topRadius;
	pc.minDistance = minDistance;
	pc.maxDistance = maxDistance;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("View Clouds");
		{
			RenderPass renderPass(viewFramebuffer, float4::zero);
			pipelineView->bind();
			pipelineView->setViewportScissor();
			pipelineView->bindDescriptorSet(cloudsDS);
			pipelineView->pushConstants(&pc);
			pipelineView->drawFullscreen();
		}
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void CloudsRenderSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("Clouds HDR Render");

	if (!isEnabled)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(blendPipeline);
	if (!pipelineView->isReady())
		return;

	if (!viewBlendDS)
	{
		auto uniforms = getBlendUniforms(graphicsSystem, cloudsView);
		viewBlendDS = graphicsSystem->createDescriptorSet(blendPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(viewBlendDS, "descriptorSet.clouds.viewBlend");
	}

	SET_GPU_DEBUG_LABEL("Blend Clouds");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(viewBlendDS);
	pipelineView->drawFullscreen();
}

void CloudsRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (cloudsView)
	{
		graphicsSystem->destroy(cloudsView);
		cloudsView = createCloudsView(graphicsSystem);
	}
	if (viewFramebuffer)
	{
		auto cloudsBufferView = graphicsSystem->get(cloudsView);
		auto framebufferView = graphicsSystem->get(viewFramebuffer);
		Framebuffer::OutputAttachment colorAttachment(
			cloudsBufferView->getDefaultView(), CloudsRenderSystem::framebufferFlags);
		framebufferView->update((uint2)cloudsBufferView->getSize(), &colorAttachment, 1);
	}
	if (viewBlendDS)
	{
		graphicsSystem->destroy(viewBlendDS);
		viewBlendDS = {};
	}
}

//**********************************************************************************************************************
Ref<Image> CloudsRenderSystem::getNoiseShape()
{
	if (!noiseShape)
		noiseShape = createNoiseShape();
	return noiseShape;
}
Ref<Image> CloudsRenderSystem::getNoiseErosion()
{
	if (!noiseErosion)
		noiseErosion = createNoiseErosion();
	return noiseErosion;
}
ID<Image> CloudsRenderSystem::getCloudsView()
{
	if (!cloudsView)
		cloudsView = createCloudsView(GraphicsSystem::Instance::get());
	return cloudsView;
}
ID<Framebuffer> CloudsRenderSystem::getViewFramebuffer()
{
	if (!viewFramebuffer)
		viewFramebuffer = createFramebuffer(GraphicsSystem::Instance::get(), getCloudsView());
	return viewFramebuffer;
}
ID<GraphicsPipeline> CloudsRenderSystem::getCloudsPipeline()
{
	if (!cloudsPipeline)
		cloudsPipeline = createCloudsPipeline(getViewFramebuffer(), quality);
	return cloudsPipeline;
}
ID<GraphicsPipeline> CloudsRenderSystem::getBlendPipeline()
{
	if (!blendPipeline)
		blendPipeline = createBlendPipeline();
	return blendPipeline;
}