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

#include "garden/system/render/csm.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"
#include "math/matrix/projection.hpp"

using namespace garden;

static void createDataBuffers(DescriptorSetBuffers& dataBuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	dataBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform, Buffer::Access::SequentialWrite, 
			sizeof(CsmRenderSystem::ShadowData), Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(buffer, "buffer.uniform.csm.data" + to_string(i));
		dataBuffers[i].resize(1); dataBuffers[i][0] = buffer;
	}
}

//**********************************************************************************************************************
static ID<Image> createShadowData(vector<ID<ImageView>>& imageViews, uint32 shadowMapSize)
{
	constexpr auto shadowFormat = Image::Format::UnormD16;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(shadowFormat, Image::Bind::DepthStencilAttachment | Image::Bind::Sampled, 
		{ Image::Layers(CsmRenderSystem::cascadeCount) }, uint2(shadowMapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.csm.shadowMap");
	imageViews.resize(CsmRenderSystem::cascadeCount);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, shadowFormat, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.csm.shadowCascade" + to_string(i));
		imageViews[i] = imageView;
	}
	
	return image;
}
static ID<Image> createTransparentData(vector<ID<ImageView>>& imageViews, uint32 shadowMapSize)
{
	constexpr auto transparentFormat = Image::Format::UfloatB10G11R11;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(transparentFormat, Image::Bind::ColorAttachment | Image::Bind::Sampled, 
		{ Image::Layers(CsmRenderSystem::cascadeCount) }, uint2(shadowMapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.csm.transparentMap");
	imageViews.resize(CsmRenderSystem::cascadeCount);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, transparentFormat, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.csm.transparentCascade" + to_string(i));
		imageViews[i] = imageView;
	}
	
	return image;
}

//**********************************************************************************************************************
static void createShadowFramebuffers(const vector<ID<ImageView>>& imageViews,
	vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	framebuffers.resize(CsmRenderSystem::cascadeCount);
	Framebuffer::OutputAttachment depthStencilAttachment({}, true, false, true);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments;
		depthStencilAttachment.imageView = imageViews[i];
		auto framebuffer = graphicsSystem->createFramebuffer(uint2(shadowMapSize), 
			std::move(colorAttachments), depthStencilAttachment);
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.csm.shadowCascade" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void createTransparentFramebuffers(const vector<ID<ImageView>>& transImageViews,
	const vector<ID<ImageView>>& shadowImageViews, vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	framebuffers.resize(CsmRenderSystem::cascadeCount);
	Framebuffer::OutputAttachment depthStencilAttachment({}, false, true, false);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(transImageViews[i], true, false, true) };
		depthStencilAttachment.imageView = shadowImageViews[i];
		auto framebuffer = graphicsSystem->createFramebuffer(uint2(shadowMapSize), 
			std::move(colorAttachments), depthStencilAttachment);
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.csm.transparentCascade" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createPipeline()
{
	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
	GARDEN_ASSERT(pbrLightingSystem->useShadowBuffer());
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("csm", pbrLightingSystem->getShadowFramebuffers()[0]);
}

static map<string, DescriptorSet::Uniform> getUniforms(ID<Image> shadowMap, 
	ID<Image> transparentMap, const DescriptorSetBuffers& dataBuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	auto shadowMapView = graphicsSystem->get(shadowMap);
	auto transparentMapView = graphicsSystem->get(transparentMap);
	auto gFramebuffer = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());
	auto depthStencilAttachment = gFramebuffer->getDepthStencilAttachment();
	
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView, 1, swapchainSize) },
		{ "shadowData", DescriptorSet::Uniform(dataBuffers) },
		{ "shadowMap", DescriptorSet::Uniform(shadowMapView->getDefaultView(), 1, swapchainSize) },
		{ "transparentMap", DescriptorSet::Uniform(transparentMapView->getDefaultView(), 1, swapchainSize) }
	};
	return uniforms;
}

//**********************************************************************************************************************
CsmRenderSystem::CsmRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", CsmRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CsmRenderSystem::deinit);
}
CsmRenderSystem::~CsmRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CsmRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CsmRenderSystem::deinit);
	}

	unsetSingleton();
}

void CsmRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("ShadowRender", CsmRenderSystem::shadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CsmRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getInt("csm.shadowMapSize", shadowMapSize);

	if (!pipeline)
		pipeline = createPipeline();
	if (dataBuffers.empty())
		createDataBuffers(dataBuffers);
	if (!shadowMap)
		shadowMap = createShadowData(shadowImageViews, shadowMapSize);
	if (!transparentMap)
		transparentMap = createTransparentData(transImageViews, shadowMapSize);
	
	if (shadowFramebuffers.empty())
		createShadowFramebuffers(shadowImageViews, shadowFramebuffers, shadowMapSize);
	if (transFramebuffers.empty())
		createTransparentFramebuffers(transImageViews, shadowImageViews, transFramebuffers, shadowMapSize);
}
void CsmRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(transFramebuffers);
		graphicsSystem->destroy(shadowFramebuffers);
		graphicsSystem->destroy(transImageViews);
		graphicsSystem->destroy(shadowImageViews);
		graphicsSystem->destroy(transparentMap);
		graphicsSystem->destroy(shadowMap);
		graphicsSystem->destroy(dataBuffers);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("ShadowRender", CsmRenderSystem::shadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CsmRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void CsmRenderSystem::shadowRender()
{
	SET_CPU_ZONE_SCOPED("Cascade Shadow Mapping");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	if (cameraConstants.shadowColor.getW() <= 0.0f)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(shadowMap, transparentMap, dataBuffers);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.csm");
	}

	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	auto dataBufferView = graphicsSystem->get(dataBuffers[swapchainIndex][0]);
	dataBufferView->flush();

	SET_GPU_DEBUG_LABEL("Cascade Shadow Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, swapchainIndex);
	pipelineView->drawFullscreen();

	PbrLightingRenderSystem::Instance::get()->markAnyShadow();
}

void CsmRenderSystem::gBufferRecreate()
{
	if (descriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms(shadowMap, transparentMap, dataBuffers);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.csm");
	}
}

//**********************************************************************************************************************
uint32 CsmRenderSystem::getShadowPassCount()
{
	return cascadeCount;
}

static f32x4x4 calcLightViewProj(const f32x4x4& view, f32x4 lightDir, f32x4& cameraOffset, 
	float fieldOfView, float aspectRatio, float nearPlane, float farPlane, float zCoeff) noexcept
{
	auto proj = (f32x4x4)calcPerspProjRevZ(fieldOfView, aspectRatio, nearPlane, farPlane);
	auto invViewProj = inverse4x4(proj * view);

	f32x4 frustumCorners[8]; uint8 cornerIndex = 0;
	for (int z = 0; z < 2; z++)
	{
		for (int y = 0; y < 2; y++)
		{
			for (int x = 0; x < 2; x++)
			{
				auto corner = invViewProj * f32x4(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z, 1.0f);
				frustumCorners[cornerIndex++] = corner / corner.getW();
			}
		}
	}

	auto center = f32x4::zero;
	for (int i = 0; i < 8; i++)
		center += frustumCorners[i];
	center *= (1.0f / 8.0f);

	auto lightView = lookAt(center - lightDir, center);
	auto minimum = f32x4::max, maximum = f32x4::minusMax;

	for (int i = 0; i < 8; i++)
	{
		auto trf = lightView * frustumCorners[i];
		minimum = min(minimum, trf);
		maximum = max(maximum, trf);
	}

	minimum.setZ(minimum.getZ() < 0.0f ? minimum.getZ() * zCoeff : minimum.getZ() / zCoeff);
	maximum.setZ(maximum.getZ() < 0.0f ? maximum.getZ() / zCoeff : maximum.getZ() * zCoeff);

	cameraOffset = -(lightDir * minimum.getZ() + center);
	auto lightProj = (f32x4x4)calcOrthoProjRevZ(float2(minimum.getX(), maximum.getX()),
		float2(minimum.getY(), maximum.getY()), float2(minimum.getZ(), maximum.getZ()));
	return lightProj * lightView;
}

//**********************************************************************************************************************
bool CsmRenderSystem::prepareShadowRender(uint32 passIndex, f32x4x4& viewProj, f32x4& cameraOffset)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	if (cameraConstants.shadowColor.getW() <= 0.0f || !graphicsSystem->camera || !graphicsSystem->directionalLight)
		return false;

	auto cameraView = CameraSystem::Instance::get()->tryGetComponent(graphicsSystem->camera);
	if (!cameraView)
		return false;

	auto nearPlane = cameraView->p.perspective.nearPlane;
	if (passIndex > 0)
		nearPlane = distance * cascadeSplits[passIndex - 1];
	auto farPlane = distance;
	if (passIndex < cascadeCount - 1)
		farPlane *= cascadeSplits[passIndex];
	farPlanes[passIndex] = farPlane;

	viewProj = calcLightViewProj(cameraConstants.view, cameraConstants.lightDir, cameraOffset,
		cameraView->p.perspective.fieldOfView, cameraView->p.perspective.aspectRatio, nearPlane, farPlane, zCoeff);

	const f32x4x4 ndcToCoords
	(
		0.5f, 0.0f, 0.0f, 0.5f,
		0.0f, 0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);
	const f32x4x4 coordsToNDC
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	auto dataBufferView = graphicsSystem->get(dataBuffers[swapchainIndex][0]);
	auto data = (ShadowData*)dataBufferView->getMap();
	data->lightSpace[passIndex] = (float4x4)(ndcToCoords * viewProj * cameraConstants.invViewProj * coordsToNDC);
	data->farPlanesIntens = (float4)f32x4(
		cameraConstants.nearPlane / farPlanes, 1.0f - cameraConstants.shadowColor.getW());
	return true;
}

//**********************************************************************************************************************
bool CsmRenderSystem::beginShadowRender(uint32 passIndex, MeshRenderType renderType)
{
	ID<Framebuffer> framebuffer; const f32x4* clearColors; uint8 clearColorCount;
	if (renderType == MeshRenderType::Opaque)
	{
		framebuffer = shadowFramebuffers[passIndex];
		clearColors = nullptr;
		clearColorCount = 0;
	}
	else if (renderType == MeshRenderType::Translucent || renderType == MeshRenderType::OIT)
	{
		framebuffer = transFramebuffers[passIndex];
		clearColors = &f32x4::one;
		clearColorCount = 1;
	}
	else abort();

	auto asyncRecording = MeshRenderSystem::Instance::get()->useAsyncRecording();
	auto framebufferView = GraphicsSystem::Instance::get()->get(framebuffer);
	framebufferView->beginRenderPass(clearColors, clearColorCount, 0.0f, 0, i32x4::zero, asyncRecording);
	GraphicsPipeline::setDepthBiasAsync(biasConstantFactor, 0.0f, biasSlopeFactor);
	return true;
}
void CsmRenderSystem::endShadowRender(uint32 passIndex, MeshRenderType renderType)
{
	ID<Framebuffer> framebuffer;
	if (renderType == MeshRenderType::Opaque)
		framebuffer = shadowFramebuffers[passIndex];
	else if (renderType == MeshRenderType::Translucent || renderType == MeshRenderType::OIT)
		framebuffer = transFramebuffers[passIndex];
	else abort();

	auto framebufferView = GraphicsSystem::Instance::get()->get(framebuffer);
	framebufferView->endRenderPass();
}

//**********************************************************************************************************************
void CsmRenderSystem::setShadowMapSize(uint32 size)
{
	abort(); // TODO:
}

ID<GraphicsPipeline> CsmRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}
const DescriptorSetBuffers& CsmRenderSystem::getDataBuffers()
{
	if (dataBuffers.empty())
		createDataBuffers(dataBuffers);
	return dataBuffers;
}

ID<Image> CsmRenderSystem::getShadowMap()
{
	if (!shadowMap)
		shadowMap = createShadowData(shadowImageViews, shadowMapSize);
	return shadowMap;
}
ID<Image> CsmRenderSystem::getTransparentMap()
{
	if (!transparentMap)
		transparentMap = createTransparentData(transImageViews, shadowMapSize);
	return transparentMap;
}

const vector<ID<Framebuffer>>& CsmRenderSystem::getShadowFramebuffers()
{
	if (!shadowMap)
		shadowMap = createShadowData(shadowImageViews, shadowMapSize);
	if (shadowFramebuffers.empty())
		createShadowFramebuffers(shadowImageViews, shadowFramebuffers, shadowMapSize);
	return shadowFramebuffers;
}
const vector<ID<Framebuffer>>& CsmRenderSystem::getTransFramebuffers()
{
	if (!transparentMap)
		transparentMap = createTransparentData(transImageViews, shadowMapSize);
	if (transFramebuffers.empty())
		createTransparentFramebuffers(transImageViews, shadowImageViews, transFramebuffers, shadowMapSize);
	return transFramebuffers;
}