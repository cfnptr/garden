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

#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"
#include "common/gbuffer.h"
#include "math/brdf.hpp"

// TODO: allow to disable OIT and other buffers creation/usage.

using namespace garden;

static void createGBuffers(GraphicsSystem* graphicsSystem, 
	vector<ID<Image>>& gBuffers, const DeferredRenderSystem::Options& options)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;

	Image::Format formats[G_BUFFER_COUNT]
	{
		DeferredRenderSystem::gBufferFormat0,
		DeferredRenderSystem::gBufferFormat1,
		DeferredRenderSystem::gBufferFormat2,
		options.useVelocity ? DeferredRenderSystem::gBufferFormat3 : Image::Format::Undefined
	};

	const Image::Mips mips = { { nullptr } };
	auto frameSize = graphicsSystem->getScaledFrameSize();
	gBuffers.resize(G_BUFFER_COUNT); auto gBufferData = gBuffers.data();

	for (uint8 i = 0; i < G_BUFFER_COUNT; i++)
	{
		if (formats[i] == Image::Format::Undefined)
		{
			gBufferData[i] = {};
			continue;
		}

		auto gBuffer = graphicsSystem->createImage(formats[i], usage, mips, frameSize, strategy);
		SET_RESOURCE_DEBUG_NAME(gBuffer, "image.deferred.gBuffer" + to_string(i));
		gBufferData[i] = gBuffer;
	}
}

static ID<Image> createDepthStencilBuffer(GraphicsSystem* graphicsSystem, bool useStencil, bool isCopy)
{
	auto image = graphicsSystem->createImage(useStencil ? DeferredRenderSystem::depthStencilFormat :
		DeferredRenderSystem::depthFormat, Image::Usage::DepthStencilAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.depthStencil" + string(isCopy ? "Copy" : ""));
	return image;
}

//**********************************************************************************************************************
static ID<Image> createHdrBuffer(GraphicsSystem* graphicsSystem, bool isCopy, bool isFullSize)
{
	auto frameSize = isFullSize ? graphicsSystem->getFramebufferSize() : graphicsSystem->getScaledFrameSize();

	uint8 lodCount = 1, layerCount = 1;
	if (isCopy)
	{
		lodCount = brdf::calcGgxBlurLodCount(frameSize);
		layerCount = 2;
	}

	Image::Mips mips(lodCount);
	for (auto& mip : mips) mip.resize(layerCount);

	auto image = graphicsSystem->createImage(DeferredRenderSystem::hdrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::Fullscreen | Image::Usage::TransferSrc | 
		Image::Usage::TransferDst, mips, frameSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.hdr" + 
		string(isCopy ? "Copy" : "") + string(isFullSize ? "Full" : ""));
	return image;
}
static ID<Image> createLdrBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::ldrBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ldr");
	return image;
}
static ID<Image> createUiBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::uiBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen | Image::Usage::TransferSrc | Image::Usage::TransferDst,
		{ { nullptr } }, graphicsSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.ui");
	return image;
}

static ID<Image> createOitAccumBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitAccumBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitAccum");
	return image;
}
static ID<Image> createOitRevealBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::oitRevealBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.oitReveal");
	return image;
}

static ID<Image> createTransBuffer(GraphicsSystem* graphicsSystem)
{
	auto image = graphicsSystem->createImage(DeferredRenderSystem::transBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Fullscreen, 
		{ { nullptr } }, graphicsSystem->getScaledFrameSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.trans");
	return image;
}
static ID<Image> createDisocclMap(GraphicsSystem* graphicsSystem)
{
	auto frameSize = graphicsSystem->getScaledFrameSize();
	auto mipCount = calcMipCount(frameSize);

	Image::Mips mips(mipCount);
	for (auto& mip : mips) mip.resize(1);

	auto image = graphicsSystem->createImage(DeferredRenderSystem::disocclMapFormat, 
		Image::Usage::TransferSrc | Image::Usage::TransferDst | Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Fullscreen, mips, frameSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.deferred.disoccl");
	return image;
}

//**********************************************************************************************************************
static ID<ImageView> createDepthStencilIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer, bool useStencil)
{
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, Image::Type::Texture2D, 
		useStencil ? DeferredRenderSystem::depthStencilFormat : DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthStencil");
	return imageView;
}
static ID<ImageView> createDepthCopyIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depthCopy");
	return imageView;
}
static ID<ImageView> createDepthOnlyIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatStencilOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::depthFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.depth");
	return imageView;
}
static ID<ImageView> createStencilOnlyIV(GraphicsSystem* graphicsSystem, ID<Image> depthStencilBuffer)
{
	auto bufferView = graphicsSystem->get(depthStencilBuffer);
	if (isFormatDepthOnly(bufferView->getFormat()))
		return {};

	auto imageView = graphicsSystem->createImageView(depthStencilBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::stencilFormat);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.stencil");
	return imageView;
}

static ID<ImageView> createHdrCopyIV(GraphicsSystem* graphicsSystem, ID<Image> hdrCopyBuffer)
{
	auto imageView = graphicsSystem->createImageView(hdrCopyBuffer, 
		Image::Type::Texture2D, DeferredRenderSystem::hdrBufferFormat, 0, 1);
	SET_RESOURCE_DEBUG_NAME(imageView, "imageView.deferred.hdrCopy");
	return imageView;
}

//**********************************************************************************************************************
static ID<Framebuffer> createGFramebuffer(GraphicsSystem* graphicsSystem, 
	const vector<ID<Image>> gBuffers, ID<ImageView> depthStencilIV)
{
	vector<Framebuffer::Attachment> colorAttachments(G_BUFFER_COUNT);
	auto colorAttachmentData = colorAttachments.data(); auto gBufferData = gBuffers.data();

	for (uint8 i = 0; i < G_BUFFER_COUNT; i++)
	{
		if (!gBufferData[i])
		{
			colorAttachmentData[i] = {};
			continue;
		}

		colorAttachmentData[i] = Framebuffer::Attachment(graphicsSystem->get(
			gBufferData[i])->getView(), Framebuffer::LoadOp::DontCare, Framebuffer::StoreOp::Store);
	}

	Framebuffer::Attachment depthStencilAttachment(depthStencilIV, 
		Framebuffer::LoadOp::Clear, Framebuffer::StoreOp::Store);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment); 
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.gBuffer");
	return framebuffer;
}

static ID<Framebuffer> createHdrFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> hdrBuffer, bool isFullSize)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(hdrBuffer)->getView())
	};
	auto frameSize = isFullSize ? graphicsSystem->getFramebufferSize() : graphicsSystem->getScaledFrameSize();
	auto framebuffer = graphicsSystem->createFramebuffer(frameSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.hdr" + string(isFullSize ? "Full" : ""));
	return framebuffer;
}
static ID<Framebuffer> createDepthStencilHdrFB(GraphicsSystem* graphicsSystem, 
	ID<Image> hdrBuffer, ID<ImageView> depthStencilIV)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(hdrBuffer)->getView())
	};
	Framebuffer::Attachment depthStencilAttachment(depthStencilIV);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthStencilHdr");
	return framebuffer;
}
static ID<Framebuffer> createLdrFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> ldrBuffer)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(ldrBuffer)->getView())
	};
	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ldr");
	return framebuffer;
}
static ID<Framebuffer> createDepthStencilLdrFB(GraphicsSystem* graphicsSystem, 
	ID<Image> ldrBuffer, ID<ImageView> depthStencilIV)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(ldrBuffer)->getView())
	};
	Framebuffer::Attachment depthStencilAttachment(depthStencilIV);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.depthStencilLdr");
	return framebuffer;
}
static ID<Framebuffer> createDisocclusionFB(GraphicsSystem* graphicsSystem, ID<Image> disocclMap)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(disocclMap)->getView(0, 0))
	};
	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getScaledFrameSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.disocclMap");
	return framebuffer;
}

//**********************************************************************************************************************
static ID<Framebuffer> createUiFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> uiBuffer)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(uiBuffer)->getView())
	};
	auto framebuffer = graphicsSystem->createFramebuffer(
		graphicsSystem->getFramebufferSize(), std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.ui");
	return framebuffer;
}
static ID<Framebuffer> createOitFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> oitAccumBuffer, ID<Image> oitRevealBuffer, ID<ImageView> depthStencilIV)
{
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(oitAccumBuffer)->getView(), 
			Framebuffer::LoadOp::Clear, Framebuffer::StoreOp::Store),
		Framebuffer::Attachment(graphicsSystem->get(oitRevealBuffer)->getView(), 
			Framebuffer::LoadOp::Clear, Framebuffer::StoreOp::Store)
	};
	Framebuffer::Attachment depthStencilAttachment(depthStencilIV);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.oit");
	return framebuffer;
}
static ID<Framebuffer> createTransDepthFB(GraphicsSystem* graphicsSystem, 
	ID<Image> transBuffer, ID<ImageView> depthOnlyIV)
{
	GARDEN_ASSERT(graphicsSystem->get(transBuffer)->getFormat() == Image::Format::UnormR8);
	vector<Framebuffer::Attachment> colorAttachments =
	{
		Framebuffer::Attachment(graphicsSystem->get(transBuffer)->getView(), 
			Framebuffer::LoadOp::Clear, Framebuffer::StoreOp::Store)
	};
	Framebuffer::Attachment depthStencilAttachment(depthOnlyIV);
	auto framebuffer = graphicsSystem->createFramebuffer(graphicsSystem->getScaledFrameSize(), 
		std::move(colorAttachments), depthStencilAttachment);
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.deferred.transDepth");
	return framebuffer;
}

static ID<GraphicsPipeline> createVelocityPipeline(ID<Framebuffer> gFramebuffer, bool useAsyncRecording)
{
	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = useAsyncRecording;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("velocity", gFramebuffer, options);
}
static ID<GraphicsPipeline> createDisocclPipeline(ID<Framebuffer> disocclusionFB)
{
	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("disocclusion", disocclusionFB, options);
}
static DescriptorSet::Uniforms getVelocityUniforms(GraphicsSystem* graphicsSystem)
{
	return { { "cc", DescriptorSet::Uniform(graphicsSystem->getCommonConstantsBuffers()) } };
}
static DescriptorSet::Uniforms getDisocclUniforms(GraphicsSystem* graphicsSystem, 
	ID<ImageView> depthBuffer, ID<ImageView> depthCopyIV, ID<Framebuffer> gFramebuffer)
{
	auto gFramebufferView = graphicsSystem->get(gFramebuffer);
	auto gVelocityView = gFramebufferView->getColorAttachments()[G_BUFFER_VELOCITY].imageView;	

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "prevDepthBuffer", DescriptorSet::Uniform(depthCopyIV) },
		{ "currDepthBuffer", DescriptorSet::Uniform(depthBuffer) },
		{ "gVelocity", DescriptorSet::Uniform(gVelocityView) }
	};
	return uniforms;
}

//**********************************************************************************************************************
DeferredRenderSystem::DeferredRenderSystem(Options options, 
	bool setSingleton) : Singleton(setSingleton), options(options)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreDeferredRender");
	manager->registerEvent("DeferredRender");
	manager->registerEvent("PreHdrRender");
	manager->registerEvent("HdrRender");
	manager->registerEvent("PreDsHdrRender");
	manager->registerEvent("DsHdrRender");
	manager->registerEvent("PreRefrRender");
	manager->registerEvent("RefrRender");
	manager->registerEvent("PreTransRender");
	manager->registerEvent("TranslRender");
	manager->registerEvent("PreTransDepthRender");
	manager->registerEvent("TransDepthRender");
	manager->registerEvent("PreOitRender");
	manager->registerEvent("OitRender");
	manager->registerEvent("PreLdrRender");
	manager->registerEvent("LdrRender");
	manager->registerEvent("PreDsLdrRender");
	manager->registerEvent("DsLdrRender");
	manager->registerEvent("PostLdrToUI");
	manager->tryRegisterEvent("PreUiRender");
	manager->tryRegisterEvent("UiRender");
	manager->registerEvent("GBufferRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", DeferredRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DeferredRenderSystem::deinit);
}
DeferredRenderSystem::~DeferredRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DeferredRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DeferredRenderSystem::deinit);

		manager->unregisterEvent("PreDeferredRender");
		manager->unregisterEvent("DeferredRender");
		manager->unregisterEvent("PreHdrRender");
		manager->unregisterEvent("HdrRender");
		manager->unregisterEvent("PreDsHdrRender");
		manager->unregisterEvent("DsHdrRender");
		manager->unregisterEvent("PreRefrRender");
		manager->unregisterEvent("RefrRender");
		manager->unregisterEvent("PreTransRender");
		manager->unregisterEvent("TransRender");
		manager->unregisterEvent("PreTransDepthRender");
		manager->unregisterEvent("TransDepthRender");
		manager->unregisterEvent("PreOitRender");
		manager->unregisterEvent("OitRender");
		manager->unregisterEvent("PreLdrRender");
		manager->unregisterEvent("LdrRender");
		manager->unregisterEvent("PreDsLdrRender");
		manager->unregisterEvent("DsLdrRender");
		manager->unregisterEvent("PostLdrToUI");
		manager->tryUnregisterEvent("PreUiRender");
		manager->tryUnregisterEvent("UiRender");
		manager->unregisterEvent("GBufferRecreate");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void DeferredRenderSystem::init()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	GARDEN_ASSERT(options.useAsyncRecording == graphicsSystem->useAsyncRecording());

	if (!velocityPipeline)
		velocityPipeline = createVelocityPipeline(getGFramebuffer(), options.useAsyncRecording);

	if (options.useDisoccl)
	{
		if (!disocclPipeline)
			disocclPipeline = createDisocclPipeline(getDisocclusionFB());
	}

	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Render", DeferredRenderSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);
}
void DeferredRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(disocclDS);
		graphicsSystem->destroy(velocityDS);
		graphicsSystem->destroy(disocclusionFB);
		graphicsSystem->destroy(upscaleHdrFB);
		graphicsSystem->destroy(transDepthFB);
		graphicsSystem->destroy(oitFramebuffer);
		graphicsSystem->destroy(uiFramebuffer);
		graphicsSystem->destroy(depthStencilLdrFB);
		graphicsSystem->destroy(ldrFramebuffer);
		graphicsSystem->destroy(depthStencilHdrFB);
		graphicsSystem->destroy(hdrFramebuffer);
		graphicsSystem->destroy(gFramebuffer);
		graphicsSystem->destroy(hdrCopyIV);
		graphicsSystem->destroy(stencilOnlyIV);
		graphicsSystem->destroy(depthOnlyIV);
		graphicsSystem->destroy(depthCopyIV);
		graphicsSystem->destroy(depthStencilIV);
		if (upscaleHdrBuffer != hdrBuffer)
			graphicsSystem->destroy(upscaleHdrBuffer);
		graphicsSystem->destroy(transBuffer);
		graphicsSystem->destroy(depthCopyBuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		graphicsSystem->destroy(disocclMap);
		if (!gBuffers.empty() && uiBuffer != gBuffers[0])
			graphicsSystem->destroy(uiBuffer);
		graphicsSystem->destroy(ldrBuffer);
		graphicsSystem->destroy(hdrCopyBlurDSes);
		graphicsSystem->destroy(hdrCopyBlurFBs);
		graphicsSystem->destroy(hdrCopyBlurPipeline);
		graphicsSystem->destroy(disocclPipeline);
		graphicsSystem->destroy(velocityPipeline);
		graphicsSystem->destroy(hdrCopyBuffer);
		graphicsSystem->destroy(hdrBuffer);
		graphicsSystem->destroy(gBuffers);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Render", DeferredRenderSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", DeferredRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void DeferredRenderSystem::render()
{
	SET_CPU_ZONE_SCOPED("Deferred Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto manager = Manager::Instance::get();
	auto cameraView = manager->tryGet<CameraComponent>(graphicsSystem->camera);
	auto transformView = manager->tryGet<TransformComponent>(graphicsSystem->camera);
	if (!cameraView || !transformView || !transformView->isActive())
		return;

	auto velocityPipelineView = graphicsSystem->get(velocityPipeline);
	if (!velocityPipelineView->isReady())
		return;

	if (options.useDisoccl)
	{
		auto disocclPipelineView = graphicsSystem->get(disocclPipeline);
		if (!disocclPipelineView->isReady())
			return;
	}

	#if GARDEN_DEBUG
	if (ForwardRenderSystem::Instance::tryGet())
	{
		GARDEN_ASSERT_MSG(!ForwardRenderSystem::Instance::get()->isEnabled, 
			"Can not use deferred and forward render system at the same time"); 
	}
	#endif

	auto event = &manager->getEvent("PreDeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Deferred Render");
		event->run();
	}
	event = &manager->getEvent("DeferredRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Deferred Render Pass");

		if (!velocityDS)
		{
			auto uniforms = getVelocityUniforms(graphicsSystem);
			velocityDS = graphicsSystem->createDescriptorSet(velocityPipeline, std::move(uniforms));
			SET_RESOURCE_DEBUG_NAME(velocityDS, "descriptorSet.deferred.velocity");
		}

		static const array<float4, G_BUFFER_COUNT> clearColors = 
		{ float4::zero, float4::zero, float4::zero, float4::zero };
		auto inFlightIndex = graphicsSystem->getInFlightIndex();

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			if (options.useDisoccl)
				Image::copy(depthStencilBuffer, getDepthCopyBuffer());

			SET_GPU_DEBUG_LABEL("Deferred Pass");
			{
				RenderPass renderPass(getGFramebuffer(), clearColors,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();

				auto pipelineView = graphicsSystem->get(velocityPipeline); // Note: do not move.
				if (options.useAsyncRecording)
				{
					pipelineView->bindAsync(0, INT32_MAX);
					pipelineView->setViewportScissorAsync(float4::zero, INT32_MAX);
					pipelineView->bindDescriptorSetAsync(velocityDS, inFlightIndex, INT32_MAX);
					pipelineView->drawFullscreenAsync(INT32_MAX);
				}
				else
				{
					pipelineView->bind();
					pipelineView->setViewportScissor();
					pipelineView->bindDescriptorSet(velocityDS, inFlightIndex);
					pipelineView->drawFullscreen();
				}
			}
		}
		if (options.useDisoccl)
		{
			SET_GPU_DEBUG_LABEL("Disocclusion Pass");

			if (!disocclDS)
			{
				auto uniforms = getDisocclUniforms(graphicsSystem, 
					getDepthOnlyIV(), getDepthCopyIV(), getGFramebuffer());
				disocclDS = graphicsSystem->createDescriptorSet(disocclPipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(disocclDS, "descriptorSet.deferred.disoccl");
			}

			auto pipelineView = graphicsSystem->get(disocclPipeline);
			auto& cc = graphicsSystem->getCommonConstants();

			DisocclPC pc;
			pc.nearPlane = cc.nearPlane;
			pc.threshold = disocclThreshold;
			pc.velFactor = disocclVelFactor;

			auto framebuffer = getDisocclusionFB();
			auto framebufferView = graphicsSystem->get(framebuffer);
			framebufferView->updateDepthStencil(anyDisoccl ? Framebuffer::LoadOp::Load : 
				Framebuffer::LoadOp::DontCare, Framebuffer::StoreOp::Store);
			{
				RenderPass renderPass(framebuffer, float4::zero);
				pipelineView->bind();
				pipelineView->setViewportScissor();
				pipelineView->bindDescriptorSet(disocclDS);
				pipelineView->pushConstants(&pc);
				pipelineView->drawFullscreen();
			}

			auto disocclMapView = graphicsSystem->get(disocclMap);
			disocclMapView->generateMips(Sampler::Filter::Linear);
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre HDR Render");
		event->run();
	}
	event = &manager->getEvent("HdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("HDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("HDR Pass");
			{
				RenderPass renderPass(getHdrFramebuffer(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreDsHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Depth/Stencil HDR Render");
		event->run();
	}
	event = &manager->getEvent("DsHdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Depth/Stencil HDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth/Stencil HDR Pass");
			{
				RenderPass renderPass(getDepthStencilHdrFB(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreRefrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Refracted Render");
		event->run();
	}
	event = &manager->getEvent("RefrRender");
	if (anyRefr && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Refracted Render Pass");

		auto _hdrCopyBuffer = getHdrCopyBuffer();
		auto gpuProcessSystem = GpuProcessSystem::Instance::get();
		gpuProcessSystem->prepareGgxBlur(_hdrCopyBuffer, hdrCopyBlurFBs);
		
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Refracted Pass");
			{
				Image::copy(depthStencilBuffer, getDepthCopyBuffer());
				Image::copy(hdrBuffer, _hdrCopyBuffer);

				gpuProcessSystem->ggxBlur(_hdrCopyBuffer, hdrCopyBlurFBs, 
					hdrCopyBlurPipeline, hdrCopyBlurDSes);

				RenderPass renderPass(getDepthStencilHdrFB(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreTransRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Translucent Render");
		event->run();
	}
	event = &manager->getEvent("TranslRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Translucent Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Pass");
			{
				RenderPass renderPass(getDepthStencilHdrFB(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreTransDepthRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Translucent Depth Render");
		event->run();
	}
	event = &manager->getEvent("TransDepthRender");
	if (anyTransDepth && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Translucent Depth Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Translucent Depth Pass");
			{
				Image::copy(depthStencilBuffer, getDepthCopyBuffer());
				RenderPass renderPass(getTransDepthFB(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreOitRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre OIT Render");
		event->run();
	}
	event = &manager->getEvent("OitRender");
	if (anyOIT && event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("OIT Render Pass");
		static const vector<float4> clearColors = { float4::zero, float4::one };

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("OIT Pass");
			{
				RenderPass renderPass(getOitFramebuffer(), clearColors,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre LDR Render");
		event->run();
	}
	event = &manager->getEvent("LdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("LDR Render Pass");

		auto framebuffer = getLdrFramebuffer();
		auto framebufferView = graphicsSystem->get(framebuffer);
		framebufferView->updateColor(0, anyLDR ? Framebuffer::LoadOp::Load : 
			Framebuffer::LoadOp::DontCare, Framebuffer::StoreOp::Store);

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("LDR Pass");
			{
				RenderPass renderPass(framebuffer, float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreDsLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre Depth/Stencil LDR Render");
		event->run();
	}
	event = &manager->getEvent("DsLdrRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Depth/Stencil LDR Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Depth/Stencil LDR Pass");
			{
				RenderPass renderPass(getDepthStencilLdrFB(), float4::zero);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	event = &manager->getEvent("PreUiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Pre UI Render");
		event->run();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy LDR to UI");
		auto _uiBuffer = getUiBuffer();
		if (!gBuffers.empty() && _uiBuffer == gBuffers[0])
			Image::copy(ldrBuffer, _uiBuffer);
		else Image::blit(ldrBuffer, _uiBuffer, Sampler::Filter::Linear);
	}
	graphicsSystem->stopRecording();

	event = &manager->getEvent("PostLdrToUI");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("Post LDR to UI Copy");
		event->run();
	}

	event = &manager->getEvent("UiRender");
	if (event->hasSubscribers())
	{
		SET_CPU_ZONE_SCOPED("UI Render Pass");
		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("UI Pass");
			{
				RenderPass renderPass(getUiFramebuffer(), float4::zero,
					0.0f, 0x00, int4::zero, options.useAsyncRecording);
				event->run();
			}
		}
		graphicsSystem->stopRecording();
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	const auto& colorAttachment = framebufferView->getColorAttachments()[0];
	auto swapchainImageView = graphicsSystem->get(colorAttachment.imageView);

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Copy UI to Swapchain");
		if (uiBufferFormat == swapchainImageView->getFormat())
			Image::copy(uiBuffer, swapchainImageView->getImage());
		else Image::blit(uiBuffer, swapchainImageView->getImage(), Sampler::Filter::Nearest);
	}
	graphicsSystem->stopRecording();

	anyDisoccl = anyRefr = anyOIT = anyTransDepth = anyLDR = false;
}

//**********************************************************************************************************************
void DeferredRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		if (!gBuffers.empty() && uiBuffer != gBuffers[0])
			graphicsSystem->destroy(uiBuffer);
		if (upscaleHdrBuffer != hdrBuffer)
			graphicsSystem->destroy(upscaleHdrBuffer);
		uiBuffer = {}; upscaleHdrBuffer = {}; // Resetting in any case.

		graphicsSystem->destroy(disocclDS);
		graphicsSystem->destroy(hdrCopyIV);
		graphicsSystem->destroy(stencilOnlyIV);
		graphicsSystem->destroy(depthOnlyIV);
		graphicsSystem->destroy(depthCopyIV);
		graphicsSystem->destroy(depthStencilIV);
		graphicsSystem->destroy(hdrCopyBlurDSes);
		graphicsSystem->destroy(hdrCopyBlurFBs);
		graphicsSystem->destroy(transBuffer);
		graphicsSystem->destroy(depthCopyBuffer);
		graphicsSystem->destroy(depthStencilBuffer);
		graphicsSystem->destroy(oitRevealBuffer);
		graphicsSystem->destroy(oitAccumBuffer);
		graphicsSystem->destroy(disocclMap);
		graphicsSystem->destroy(ldrBuffer);
		graphicsSystem->destroy(hdrCopyBuffer);
		graphicsSystem->destroy(hdrBuffer);
		graphicsSystem->destroy(gBuffers); gBuffers.clear();

		auto frameSize = graphicsSystem->getScaledFrameSize();
		auto depthStencilIV = getDepthStencilIV();

		if (gFramebuffer)
		{
			auto _gBuffers = getGBuffers();
			auto framebufferView = graphicsSystem->get(gFramebuffer);

			ID<ImageView> colorAttachments[G_BUFFER_COUNT];
			for (uint8 i = 0; i < G_BUFFER_COUNT; i++)
			{
				colorAttachments[i] = _gBuffers[i] ? graphicsSystem->get(
					_gBuffers[i])->getView() : ID<ImageView>();
			}
			framebufferView->update(frameSize, colorAttachments, G_BUFFER_COUNT, depthStencilIV);
		}
		if (hdrFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(hdrFramebuffer);
			framebufferView->update(frameSize, graphicsSystem->get(getHdrBuffer())->getView());
		}
		if (depthStencilHdrFB)
		{
			auto framebufferView = graphicsSystem->get(depthStencilHdrFB);
			framebufferView->update(frameSize, graphicsSystem->get(getHdrBuffer())->getView(), depthStencilIV);
		}
		if (ldrFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(ldrFramebuffer);
			framebufferView->update(frameSize, graphicsSystem->get(getLdrBuffer())->getView());
		}
		if (depthStencilLdrFB)
		{
			auto framebufferView = graphicsSystem->get(depthStencilLdrFB);
			framebufferView->update(frameSize, graphicsSystem->get(getLdrBuffer())->getView(), depthStencilIV);
		}
		if (uiFramebuffer)
		{
			auto framebufferView = graphicsSystem->get(uiFramebuffer);
			framebufferView->update(graphicsSystem->getFramebufferSize(), 
				graphicsSystem->get(getUiBuffer())->getView());
		}
		if (oitFramebuffer)
		{
			ID<ImageView> colorAttachments[2];
			colorAttachments[0] = graphicsSystem->get(getOitAccumBuffer())->getView();
			colorAttachments[1] = graphicsSystem->get(getOitRevealBuffer())->getView();
			auto framebufferView = graphicsSystem->get(oitFramebuffer);
			framebufferView->update(frameSize, colorAttachments, 2, depthStencilIV);
		}
		if (transDepthFB)
		{
			auto framebufferView = graphicsSystem->get(transDepthFB);
			framebufferView->update(frameSize, graphicsSystem->get(getTransBuffer())->getView(), depthStencilIV);
		}
		if (upscaleHdrFB)
		{
			auto framebufferView = graphicsSystem->get(upscaleHdrFB);
			framebufferView->update(graphicsSystem->useUpscaling ? graphicsSystem->getFramebufferSize() : 
				frameSize, graphicsSystem->get(getUpscaleHdrBuffer())->getView());
		}
		if (disocclusionFB)
		{
			auto framebufferView = graphicsSystem->get(disocclusionFB);
			framebufferView->update(frameSize, graphicsSystem->get(getDisocclMap())->getView(0, 0));
		}
	}

	if (swapchainChanges.framebufferSize)
		Manager::Instance::get()->runEvent("GBufferRecreate");
}

void DeferredRenderSystem::setOptions(Options options)
{
	abort(); // TODO:
}

//**********************************************************************************************************************
ID<GraphicsPipeline> DeferredRenderSystem::getVelocityPipeline()
{
	if (options.useVelocity && !velocityPipeline)
		velocityPipeline = createVelocityPipeline(getGFramebuffer(), options.useAsyncRecording);
	return velocityPipeline;
}
ID<GraphicsPipeline> DeferredRenderSystem::getDisocclPipeline()
{
	if (options.useDisoccl && !disocclPipeline)
		disocclPipeline = createDisocclPipeline(getDisocclusionFB());
	return disocclPipeline;
}

const vector<ID<Image>>& DeferredRenderSystem::getGBuffers()
{
	if (gBuffers.empty())
		createGBuffers(GraphicsSystem::Instance::get(), gBuffers, options);
	return gBuffers;
}
ID<Image> DeferredRenderSystem::getHdrBuffer()
{
	if (!hdrBuffer)
		hdrBuffer = createHdrBuffer(GraphicsSystem::Instance::get(), false, false);
	return hdrBuffer;
}
ID<Image> DeferredRenderSystem::getHdrCopyBuffer()
{
	if (!hdrCopyBuffer)
		hdrCopyBuffer = createHdrBuffer(GraphicsSystem::Instance::get(), true, false);
	return hdrCopyBuffer;
}
ID<Image> DeferredRenderSystem::getLdrBuffer()
{
	if (!ldrBuffer)
		ldrBuffer = createLdrBuffer(GraphicsSystem::Instance::get());
	return ldrBuffer;
}
ID<Image> DeferredRenderSystem::getUiBuffer()
{
	if (!uiBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		uiBuffer = graphicsSystem->getScaledFrameSize() != graphicsSystem->getFramebufferSize() ? 
			createUiBuffer(graphicsSystem) : getGBuffers()[0];
	}
	return uiBuffer;
}

//**********************************************************************************************************************
ID<Image> DeferredRenderSystem::getOitAccumBuffer()
{
	if (!oitAccumBuffer)
		oitAccumBuffer = createOitAccumBuffer(GraphicsSystem::Instance::get());
	return oitAccumBuffer;
}
ID<Image> DeferredRenderSystem::getOitRevealBuffer()
{
	if (!oitRevealBuffer)
		oitRevealBuffer = createOitRevealBuffer(GraphicsSystem::Instance::get());
	return oitRevealBuffer;
}
ID<Image> DeferredRenderSystem::getDepthStencilBuffer()
{
	if (!depthStencilBuffer)
		depthStencilBuffer = createDepthStencilBuffer(GraphicsSystem::Instance::get(), options.useStencil, false);
	return depthStencilBuffer;
}
ID<Image> DeferredRenderSystem::getDepthCopyBuffer()
{
	if (!depthCopyBuffer)
		depthCopyBuffer = createDepthStencilBuffer(GraphicsSystem::Instance::get(), options.useStencil, true);
	return depthCopyBuffer;
}
ID<Image> DeferredRenderSystem::getTransBuffer()
{
	if (!transBuffer)
		transBuffer = createTransBuffer(GraphicsSystem::Instance::get());
	return transBuffer;
}
ID<Image> DeferredRenderSystem::getUpscaleHdrBuffer()
{
	if (!upscaleHdrBuffer)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		upscaleHdrBuffer = graphicsSystem->useUpscaling ? 
			createHdrBuffer(graphicsSystem, false, true) : getHdrBuffer();
	}
	return upscaleHdrBuffer;
}
ID<Image> DeferredRenderSystem::getDisocclMap()
{
	if (options.useDisoccl && !disocclMap)
		disocclMap = createDisocclMap(GraphicsSystem::Instance::get());
	return disocclMap;
}

//**********************************************************************************************************************
ID<ImageView> DeferredRenderSystem::getHdrImageView()
{
	return GraphicsSystem::Instance::get()->get(getHdrBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getHdrCopyIV()
{
	if (!hdrCopyIV)
		hdrCopyIV = createHdrCopyIV(GraphicsSystem::Instance::get(), getHdrCopyBuffer());
	return hdrCopyIV;
}
ID<ImageView> DeferredRenderSystem::getLdrImageView()
{
	return GraphicsSystem::Instance::get()->get(getLdrBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getUiImageView()
{
	return GraphicsSystem::Instance::get()->get(getUiBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getOitAccumIV()
{
	return GraphicsSystem::Instance::get()->get(getOitAccumBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getOitRevealIV()
{
	return GraphicsSystem::Instance::get()->get(getOitRevealBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getDepthStencilIV()
{
	if (!depthStencilIV)
	{
		depthStencilIV = createDepthStencilIV(GraphicsSystem::Instance::get(), 
			getDepthStencilBuffer(), options.useStencil);
	}
	return depthStencilIV;
}
ID<ImageView> DeferredRenderSystem::getDepthCopyIV()
{
	if (!depthCopyIV)
		depthCopyIV = createDepthCopyIV(GraphicsSystem::Instance::get(), getDepthCopyBuffer());
	return depthCopyIV;
}
ID<ImageView> DeferredRenderSystem::getDepthOnlyIV()
{
	if (!depthOnlyIV)
		depthOnlyIV = createDepthOnlyIV(GraphicsSystem::Instance::get(), getDepthStencilBuffer());
	return depthOnlyIV;
}
ID<ImageView> DeferredRenderSystem::getStencilOnlyIV()
{
	if (!stencilOnlyIV)
		stencilOnlyIV = createStencilOnlyIV(GraphicsSystem::Instance::get(), getDepthStencilBuffer());
	return stencilOnlyIV;
}
ID<ImageView> DeferredRenderSystem::getTransImageView()
{
	return GraphicsSystem::Instance::get()->get(getTransBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getUpscaleHdrIV()
{
	return GraphicsSystem::Instance::get()->get(getUpscaleHdrBuffer())->getView();
}
ID<ImageView> DeferredRenderSystem::getDisocclView(uint8 mip)
{
	if (!options.useDisoccl)
		return {};
	return GraphicsSystem::Instance::get()->get(getDisocclMap())->getView(0, mip);
}

//**********************************************************************************************************************
ID<Framebuffer> DeferredRenderSystem::getGFramebuffer()
{
	if (!gFramebuffer)
		gFramebuffer = createGFramebuffer(GraphicsSystem::Instance::get(), getGBuffers(), getDepthStencilIV());
	return gFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getHdrFramebuffer()
{
	if (!hdrFramebuffer)
		hdrFramebuffer = createHdrFramebuffer(GraphicsSystem::Instance::get(),getHdrBuffer(), false);
	return hdrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthStencilHdrFB()
{
	if (!depthStencilHdrFB)
	{
		depthStencilHdrFB = createDepthStencilHdrFB(GraphicsSystem::Instance::get(), 
			getHdrBuffer(), getDepthStencilIV());
	}
	return depthStencilHdrFB;
}
ID<Framebuffer> DeferredRenderSystem::getLdrFramebuffer()
{
	if (!ldrFramebuffer)
		ldrFramebuffer = createLdrFramebuffer(GraphicsSystem::Instance::get(),getLdrBuffer());
	return ldrFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getDepthStencilLdrFB()
{
	if (!depthStencilLdrFB)
	{
		depthStencilLdrFB = createDepthStencilLdrFB(GraphicsSystem::Instance::get(), 
			getLdrBuffer(), getDepthStencilIV());
	}
	return depthStencilLdrFB;
}
ID<Framebuffer> DeferredRenderSystem::getUiFramebuffer()
{
	if (!uiFramebuffer)
		uiFramebuffer = createUiFramebuffer(GraphicsSystem::Instance::get(), getUiBuffer());
	return uiFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getOitFramebuffer()
{
	if (!oitFramebuffer)
	{
		oitFramebuffer = createOitFramebuffer(GraphicsSystem::Instance::get(), 
			getOitAccumBuffer(), getOitRevealBuffer(), getDepthStencilIV());
	}
	return oitFramebuffer;
}
ID<Framebuffer> DeferredRenderSystem::getTransDepthFB()
{
	if (!transDepthFB)
		transDepthFB = createTransDepthFB(GraphicsSystem::Instance::get(), getTransBuffer(), getDepthOnlyIV());
	return transDepthFB;
}
ID<Framebuffer> DeferredRenderSystem::getUpscaleHdrFB()
{
	if (!upscaleHdrFB)
		upscaleHdrFB = createHdrFramebuffer(GraphicsSystem::Instance::get(), getUpscaleHdrBuffer(), true);
	return upscaleHdrFB;
}
ID<Framebuffer> DeferredRenderSystem::getDisocclusionFB()
{
	if (options.useDisoccl && !disocclusionFB)
		disocclusionFB = createDisocclusionFB(GraphicsSystem::Instance::get(), getDisocclMap());
	return disocclusionFB;
}
const vector<ID<Framebuffer>>& DeferredRenderSystem::getHdrCopyBlurFBs()
{
	if (hdrCopyBlurFBs.empty())
		GpuProcessSystem::Instance::get()->prepareGgxBlur(getHdrCopyBuffer(), hdrCopyBlurFBs);
	return hdrCopyBlurFBs;
}