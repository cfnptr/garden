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

#include "garden/system/render/csm.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/profiler.hpp"
#include "common/gbuffer.h"

#include "math/matrix/transform.hpp"

using namespace garden;

static void createDataBuffers(GraphicsSystem* graphicsSystem, DescriptorSet::Buffers& dataBuffers)
{
	auto inFlightCount = graphicsSystem->getInFlightCount();
	dataBuffers.resize(inFlightCount);

	for (uint32 i = 0; i < inFlightCount; i++)
	{
		// Note: we are not writing data in sequence due to shadow passes!
		auto buffer = graphicsSystem->createBuffer(Buffer::Usage::Uniform, Buffer::CpuAccess::RandomReadWrite, 
			sizeof(CsmRenderSystem::ShadowData), Buffer::Location::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(buffer, "buffer.uniform.csm.data" + to_string(i));
		dataBuffers[i].resize(1); dataBuffers[i][0] = buffer;
	}
}

static ID<Image> createDepthMap(GraphicsSystem* graphicsSystem, uint32 shadowMapSize)
{
	auto image = graphicsSystem->createImage(CsmRenderSystem::depthFormat, 
		Image::Usage::DepthStencilAttachment | Image::Usage::Sampled, { Image::Layers(CsmRenderSystem::cascadeCount) }, 
		uint2(shadowMapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.csm.depthMap");
	return image;
}
static ID<Image> createTransMap(GraphicsSystem* graphicsSystem, uint32 shadowMapSize)
{
	auto image = graphicsSystem->createImage(CsmRenderSystem::transparentFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled, { Image::Layers(CsmRenderSystem::cascadeCount) }, 
		uint2(shadowMapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.csm.transMap");
	return image;
}

//**********************************************************************************************************************
static void createShadowFramebuffers(GraphicsSystem* graphicsSystem, 
	ID<Image> depthMap, vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto depthMapView = graphicsSystem->get(depthMap);
	Framebuffer::OutputAttachment depthStencilAttachment({}, CsmRenderSystem::shadowFlags);
	auto size = uint2(shadowMapSize);

	framebuffers.resize(CsmRenderSystem::cascadeCount);
	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments;
		depthStencilAttachment.imageView = depthMapView->getView(i, 0);
		auto framebuffer = graphicsSystem->createFramebuffer(size, 
			std::move(colorAttachments), depthStencilAttachment);
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.csm.shadowCascade" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void updateShadowFramebuffers(GraphicsSystem* graphicsSystem, 
	ID<Image> depthMap, vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto depthMapView = graphicsSystem->get(depthMap);
	Framebuffer::OutputAttachment depthStencilAttachment({}, CsmRenderSystem::shadowFlags);
	auto size = uint2(shadowMapSize);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		depthStencilAttachment.imageView = depthMapView->getView(1, 0);
		auto framebufferView = graphicsSystem->get(framebuffers[i]);
		framebufferView->update(size, nullptr, 0, depthStencilAttachment);
	}
}

static void createTransparentFramebuffers(GraphicsSystem* graphicsSystem, ID<Image> depthMap,
	ID<Image> transMap, vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto depthMapView = graphicsSystem->get(depthMap);
	auto transMapView = graphicsSystem->get(transMap);
	Framebuffer::OutputAttachment depthStencilAttachment({}, CsmRenderSystem::transDepthFlags);
	auto size = uint2(shadowMapSize);

	framebuffers.resize(CsmRenderSystem::cascadeCount);
	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(transMapView->getView(i, 0), CsmRenderSystem::transColorFlags) };
		depthStencilAttachment.imageView = depthMapView->getView(i, 0);
		auto framebuffer = graphicsSystem->createFramebuffer(size, 
			std::move(colorAttachments), depthStencilAttachment);
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.csm.transparentCascade" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}
static void updateTransparentFramebuffers(GraphicsSystem* graphicsSystem, ID<Image> depthMap,
	ID<Image> transMap, vector<ID<Framebuffer>>& framebuffers, uint32 shadowMapSize)
{
	auto depthMapView = graphicsSystem->get(depthMap);
	auto transMapView = graphicsSystem->get(transMap);
	Framebuffer::OutputAttachment depthStencilAttachment({}, CsmRenderSystem::transDepthFlags);
	auto size = uint2(shadowMapSize);

	for (uint8 i = 0; i < CsmRenderSystem::cascadeCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(transMapView->getView(i, 0), CsmRenderSystem::transColorFlags) };
		depthStencilAttachment.imageView = depthMapView->getView(i, 0);
		auto framebufferView = graphicsSystem->get(framebuffers[i]);
		framebufferView->update(size, std::move(colorAttachments), depthStencilAttachment);
	}
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createPipeline()
{
	auto pbrLightingSystem = PbrLightingSystem::Instance::get();
	GARDEN_ASSERT(pbrLightingSystem->getOptions().useShadBuffer);

	ResourceSystem::GraphicsOptions options;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("csm", pbrLightingSystem->getShadBaseFB(), options);
}

static DescriptorSet::Uniforms getUniforms(GraphicsSystem* graphicsSystem, ID<Image> depthMap, 
	ID<Image> transMap, const DescriptorSet::Buffers& dataBuffers)
{
	auto depthMapView = graphicsSystem->get(depthMap)->getView();
	auto transMapView = graphicsSystem->get(transMap)->getView();
	auto hizBufferView = HizRenderSystem::Instance::get()->getView(1);
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());
	auto gNormalsView = gFramebufferView->getColorAttachments()[G_BUFFER_NORMALS].imageView;
	auto inFlightCount = graphicsSystem->getInFlightCount();
	
	DescriptorSet::Uniforms uniforms =
	{ 
		{ "gNormals", DescriptorSet::Uniform(gNormalsView, 1, inFlightCount) },
		{ "hizBuffer", DescriptorSet::Uniform(hizBufferView, 1, inFlightCount) },
		{ "shadowData", DescriptorSet::Uniform(dataBuffers) },
		{ "depthMap", DescriptorSet::Uniform(depthMapView, 1, inFlightCount) },
		{ "transMap", DescriptorSet::Uniform(transMapView, 1, inFlightCount) }
	};
	return uniforms;
}

//**********************************************************************************************************************
CsmRenderSystem::CsmRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<IShadowMeshRenderSystem>(this);

	ECSM_SUBSCRIBE_TO_EVENT("Init", CsmRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CsmRenderSystem::deinit);
}
CsmRenderSystem::~CsmRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<IShadowMeshRenderSystem>(this);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CsmRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CsmRenderSystem::deinit);
	}

	unsetSingleton();
}

void CsmRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreShadowRender", CsmRenderSystem::preShadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("ShadowRender", CsmRenderSystem::shadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", CsmRenderSystem::gBufferRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getInt("csm.shadowMapSize", shadowMapSize);
}
void CsmRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(transFramebuffers);
		graphicsSystem->destroy(shadowFramebuffers);
		graphicsSystem->destroy(transMap);
		graphicsSystem->destroy(depthMap);
		graphicsSystem->destroy(dataBuffers);
		graphicsSystem->destroy(pipeline);

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreShadowRender", CsmRenderSystem::preShadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("ShadowRender", CsmRenderSystem::shadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", CsmRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void CsmRenderSystem::preShadowRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& cc = graphicsSystem->getCommonConstants();
	hasShadows = false;

	if (!isEnabled || cc.shadowColor.getW() <= 0.0f || !graphicsSystem->directionalLight)
		return;

	if (!isInitialized)
	{
		if (!pipeline)
			pipeline = createPipeline();
		if (dataBuffers.empty())
			createDataBuffers(graphicsSystem, dataBuffers);
		if (!depthMap)
			depthMap = createDepthMap(graphicsSystem, shadowMapSize);
		if (!transMap)
			transMap = createTransMap(graphicsSystem, shadowMapSize);
		if (shadowFramebuffers.empty())
			createShadowFramebuffers(graphicsSystem, depthMap, shadowFramebuffers, shadowMapSize);
		if (transFramebuffers.empty())
			createTransparentFramebuffers(graphicsSystem, depthMap, transMap, transFramebuffers, shadowMapSize);
		isInitialized = true;
	}	

	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	PbrLightingSystem::Instance::get()->markFbShadow();
	hasShadows = true;
}
void CsmRenderSystem::shadowRender()
{
	SET_CPU_ZONE_SCOPED("Cascade Shadow Mapping");

	if (!hasShadows)
		return;
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(graphicsSystem, depthMap, transMap, dataBuffers);
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.csm");
	}

	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	auto dataBufferView = graphicsSystem->get(dataBuffers[inFlightIndex][0]);
	dataBufferView->flush();

	SET_GPU_DEBUG_LABEL("Cascade Shadow Mapping");
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, inFlightIndex);
	pipelineView->drawFullscreen();

	PbrLightingSystem::Instance::get()->markAnyShadow();
}

void CsmRenderSystem::gBufferRecreate()
{
	GraphicsSystem::Instance::get()->destroy(descriptorSet);
}

//**********************************************************************************************************************
uint8 CsmRenderSystem::getShadowPassCount()
{
	return cascadeCount;
}

static f32x4x4 calcLightViewProj(const f32x4x4& view, f32x4 lightDir, f32x4& cameraOffset, float fieldOfView, 
	float aspectRatio, float nearPlane, float farPlane, float zCoeff, uint32 shadowMapSize) noexcept
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

	float unitsPerTexel = (maximum.getX() - minimum.getX()) / shadowMapSize;
	auto lightCameraPos = lightView * center;
	lightCameraPos.setX(floor(lightCameraPos.getX() / unitsPerTexel) * unitsPerTexel);
	lightCameraPos.setZ(floor(lightCameraPos.getZ() / unitsPerTexel) * unitsPerTexel);
	auto snappedCameraPos = inverse4x4(lightView) * lightCameraPos;
	auto stabilizedLightView = lookAt(snappedCameraPos - lightDir, snappedCameraPos);

	cameraOffset = -(lightDir * minimum.getZ() + center);
	auto lightProj = (f32x4x4)calcOrthoProjRevZ(float2(minimum.getX(), maximum.getX()),
		float2(minimum.getY(), maximum.getY()), float2(minimum.getZ(), maximum.getZ()));
	return lightProj * stabilizedLightView;
}

//**********************************************************************************************************************
bool CsmRenderSystem::prepareShadowRender(uint32 passIndex, f32x4x4& viewProj, f32x4& cameraOffset)
{
	if (!hasShadows)
		return false;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto cameraView = Manager::Instance::get()->get<CameraComponent>(graphicsSystem->camera);
	auto nearPlane = cameraView->p.perspective.nearPlane;
	if (passIndex > 0)
		nearPlane = distance * cascadeSplits[passIndex - 1];
	auto farPlane = distance;
	if (passIndex < cascadeCount - 1)
		farPlane *= cascadeSplits[passIndex];
	farPlanes[passIndex] = farPlane;

	const auto& cc = graphicsSystem->getCommonConstants();
	viewProj = calcLightViewProj(cc.view, (f32x4)cc.lightDir, cameraOffset, cameraView->p.perspective.fieldOfView, 
		cameraView->p.perspective.aspectRatio, nearPlane, farPlane, zCoeff, shadowMapSize);

	auto inFlightIndex = graphicsSystem->getInFlightIndex();
	auto dataBufferView = graphicsSystem->get(dataBuffers[inFlightIndex][0]);
	auto data = (ShadowData*)dataBufferView->getMap();
	data->viewProj[passIndex] = (float4x4)viewProj;
	data->uvToLight[passIndex] = (float4x4)(f32x4x4::ndcToUV * viewProj * cc.invViewProj * f32x4x4::uvToNDC);

	if (passIndex == cascadeCount - 1)
	{
		data->farPlanes = float4((float3)(cc.nearPlane / farPlanes), 0.0f);
		data->starDir = -cc.lightDir;
		data->normBias = biasNormalFactor;
	}
	return true;
}

//**********************************************************************************************************************
bool CsmRenderSystem::beginShadowRender(uint32 passIndex, MeshRenderType renderType)
{
	ID<Framebuffer> framebuffer; const float4* clearColors; uint8 clearColorCount;
	if (renderType == MeshRenderType::Opaque)
	{
		framebuffer = shadowFramebuffers[passIndex];
		clearColors = nullptr;
		clearColorCount = 0;
	}
	else if (renderType == MeshRenderType::Translucent)
	{
		if (!renderTranslucent)
			return false;
		framebuffer = transFramebuffers[passIndex];
		clearColors = &float4::one;
		clearColorCount = 1;
	}
	else abort();

	auto asyncRecording = MeshRenderSystem::Instance::get()->useAsyncRecording();
	auto framebufferView = GraphicsSystem::Instance::get()->get(framebuffer);
	framebufferView->beginRenderPass(clearColors, clearColorCount, 0.0f, 0x00, int4::zero, asyncRecording);
	GraphicsPipeline::setDepthBiasAsync(biasConstantFactor, biasSlopeFactor);
	return true;
}
void CsmRenderSystem::endShadowRender(uint32 passIndex, MeshRenderType renderType)
{
	ID<Framebuffer> framebuffer;
	if (renderType == MeshRenderType::Opaque)
		framebuffer = shadowFramebuffers[passIndex];
	else if (renderType == MeshRenderType::Translucent)
		framebuffer = transFramebuffers[passIndex];
	else abort();

	auto framebufferView = GraphicsSystem::Instance::get()->get(framebuffer);
	framebufferView->endRenderPass();
}

//**********************************************************************************************************************
void CsmRenderSystem::setShadowMapSize(uint32 size)
{
	GARDEN_ASSERT(size > 0);
	if (shadowMapSize == size)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);

	if (depthMap)
	{
		graphicsSystem->destroy(depthMap);
		depthMap = createDepthMap(graphicsSystem, size);
		updateShadowFramebuffers(graphicsSystem, depthMap, shadowFramebuffers, size);
	}
	if (transMap)
	{
		graphicsSystem->destroy(transMap);
		transMap = createTransMap(graphicsSystem, size);
		updateTransparentFramebuffers(graphicsSystem, getDepthMap(), transMap, transFramebuffers, size);
	}

	shadowMapSize = size;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> CsmRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}
const DescriptorSet::Buffers& CsmRenderSystem::getDataBuffers()
{
	if (dataBuffers.empty())
		createDataBuffers(GraphicsSystem::Instance::get(), dataBuffers);
	return dataBuffers;
}

ID<Image> CsmRenderSystem::getDepthMap()
{
	if (!depthMap)
		depthMap = createDepthMap(GraphicsSystem::Instance::get(), shadowMapSize);
	return depthMap;
}
ID<Image> CsmRenderSystem::getTransMap()
{
	if (!transMap)
		transMap = createTransMap(GraphicsSystem::Instance::get(), shadowMapSize);
	return transMap;
}

const vector<ID<Framebuffer>>& CsmRenderSystem::getShadowFramebuffers()
{
	if (shadowFramebuffers.empty())
		createShadowFramebuffers(GraphicsSystem::Instance::get(), getDepthMap(), shadowFramebuffers, shadowMapSize);
	return shadowFramebuffers;
}
const vector<ID<Framebuffer>>& CsmRenderSystem::getTransFramebuffers()
{
	if (transFramebuffers.empty())
	{
		createTransparentFramebuffers(GraphicsSystem::Instance::get(), 
			getDepthMap(), getTransMap(), transFramebuffers, shadowMapSize);
	}
	return transFramebuffers;
}