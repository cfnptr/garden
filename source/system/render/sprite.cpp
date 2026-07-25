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

#include "garden/system/render/sprite.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"
#include "sprite/instance-data.h"

using namespace garden;

// TODO: add bindless support
// TODO: Add automatic tightly packed sprite arrays (add support of this to the resource system or texture atlases).

//**********************************************************************************************************************
static constexpr auto imageUsage = Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ;

void SpriteRenderSystem::init()
{
	InstanceRenderSystem::init();

	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);

	#if GARDEN_DEBUG
	debugResourceName = pipelinePath.generic_string();
	#endif
}

void SpriteRenderSystem::imageLoaded()
{
	auto resourceSystem = ResourceSystem::getInstance();
	auto image = resourceSystem->getLoadedImage();
	auto& imagePath = resourceSystem->getLoadedImagePaths()[0];
	auto& spriteRenderPool = getMeshComponentPool();
	auto componentSize = getMeshComponentSize();
	auto componentData = (uint8*)spriteRenderPool.getData();
	auto componentOccupancy = spriteRenderPool.getOccupancy();
	Ref<DescriptorSet> descriptorSet = {};
	
	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto spriteRenderView = (SpriteRenderComponent*)(componentData + i * componentSize);
		if (spriteRenderView->colorMap != image || spriteRenderView->descriptorSet)
			continue;
		if (!descriptorSet)
			descriptorSet = createSharedDS(imagePath.generic_string(), image);
		spriteRenderView->descriptorSet = descriptorSet;
	}

	auto& spriteFramePool = getSpriteFramePool();
	componentSize = getSpriteFrameSize();
	componentData = (uint8*)spriteFramePool.getData();
	componentOccupancy = spriteFramePool.getOccupancy();

	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto spriteFrame = (SpriteAnimFrame*)(componentData + i * componentSize);
		if (spriteFrame->colorMap != image || spriteFrame->descriptorSet)
			continue;
		if (!descriptorSet)
			descriptorSet = createSharedDS(imagePath.generic_string(), image);
		spriteFrame->descriptorSet = descriptorSet;
	}
}

void SpriteRenderSystem::resetComponent(View<Component> component)
{
	auto resourceSystem = ResourceSystem::getInstance();
	auto componentView = View<SpriteRenderComponent>(component);
	resourceSystem->destroyShared(componentView->colorMap);
	resourceSystem->destroyShared(componentView->descriptorSet);
}

//**********************************************************************************************************************
uint32 SpriteRenderSystem::getReadyMeshesAsync(MeshRenderComponent* meshRenderView, 
	const f32x4& cameraPosition, const Frustum& frustum, f32x4x4& model)
{
	if (isBehindFrustum(frustum, meshRenderView->aabb, model))
		return 0;
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;
	return spriteRenderView->descriptorSet ? 1 : 0;
}
void SpriteRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;

	DescriptorSet::Range dsRanges[2];
	dsRanges[0] = DescriptorSet::Range(descriptorSet, 1, inFlightIndex);
	dsRanges[1] = DescriptorSet::Range((ID<DescriptorSet>)spriteRenderView->descriptorSet);

	auto instanceData = (BaseInstanceData*)(instanceMap + instanceIndex * getBaseInstanceDataSize());
	setInstanceData(spriteRenderView, instanceData, viewProj, model, instanceIndex, taskIndex);

	PushConstants pc;
	setPushConstants(spriteRenderView, &pc, viewProj, model, instanceIndex, taskIndex);

	pipelineView->bindDescriptorSetsAsync(dsRanges, 2, taskIndex);
	pipelineView->pushConstantsAsync(&pc, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6);
}

uint64 SpriteRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(BaseInstanceData);
}
void SpriteRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, void* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	auto spriteData = (BaseInstanceData*)instanceData;
	spriteData->model = (float3x4)transpose4x4(model);
	spriteData->uvSize = spriteRenderView->uvSize;
	spriteData->uvOffset = spriteRenderView->uvOffset;
	spriteData->colorAdd = spriteRenderView->colorAdd;
	spriteData->colorMul = spriteRenderView->colorMul;
}
void SpriteRenderSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	pushConstants->instanceIndex = instanceIndex;
	pushConstants->colorMapLayer = spriteRenderView->getColorMapLayer();
}

//**********************************************************************************************************************
DescriptorSet::Uniforms SpriteRenderSystem::getSpriteUniforms(ID<ImageView> colorMap)
{
	DescriptorSet::Uniforms spriteUniforms = { { "colorMap", DescriptorSet::Uniform(colorMap) } };
	return spriteUniforms;
}
ID<GraphicsPipeline> SpriteRenderSystem::createBasePipeline()
{
	auto deferredSystem = DeferredRenderSystem::tryGetInstance();
	ID<Framebuffer> framebuffer; GraphicsPipeline::State pipelineState;
	if (deferredSystem)
	{
		if (getMeshRenderType() == MeshRenderType::UI)
		{
			framebuffer = deferredSystem->getUiFramebuffer();
		}
		else
		{
			framebuffer = deferredSystem->getDepthStencilHdrFB();
			pipelineState.depthTesting = pipelineState.depthWriting = true;
		}
	}
	else
	{
		framebuffer = ForwardRenderSystem::getInstance()->getColorFramebuffer();
	}
	GraphicsPipeline::PipelineStates pipelineStates = { { 0, pipelineState } };

	ResourceSystem::GraphicsLoadOptions options;
	options.loadAsync = false; // We can't load async due to imageLoaded() usage.
	options.pipelineStateOverrides = &pipelineStates;
	return ResourceSystem::getInstance()->loadGraphicsPipeline(pipelinePath, framebuffer, &options);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<SpriteRenderComponent>(component);
	if (componentView->aabb != Aabb::one)
		serializer.write("aabb", componentView->aabb);
	if (!componentView->isEnabled)
		serializer.write("isEnabled", false);
	if (componentView->getColorMapLayer() != 0.0f)
		serializer.write("colorMapLayer", componentView->getColorMapLayer());
	if (componentView->uvSize != half2::one)
		serializer.write("uvSize", componentView->uvSize);
	if (componentView->uvOffset != half2::zero)
		serializer.write("uvOffset", componentView->uvOffset);
	if (componentView->colorAdd != Color::transparent)
		serializer.write("colorAdd", componentView->colorAdd);
	if (componentView->colorMul != Color::white)
		serializer.write("colorMul", componentView->colorMul);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!componentView->colorMapPath.empty())
		serializer.write("colorMapPath", componentView->colorMapPath.generic_string());
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<SpriteRenderComponent>(component);
	deserializer.read("aabb", componentView->aabb);
	deserializer.read("isEnabled", componentView->isEnabled);
	deserializer.read("colorMapLayer", componentView->_colorMapLayer());
	deserializer.read("uvSize", componentView->uvSize);
	deserializer.read("uvOffset", componentView->uvOffset);
	deserializer.read("colorAdd", componentView->colorAdd);
	deserializer.read("colorMul", componentView->colorMul);

	if (deserializer.read("colorMapPath", valueStringCache))
	{
		if (valueStringCache.empty()) valueStringCache.assign("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		componentView->colorMapPath = valueStringCache;
		#endif
		componentView->colorMap = ResourceSystem::getInstance()->loadSharedImage(valueStringCache);
	}
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<SpriteAnimFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
	if (frameView->animateUvSize)
		serializer.write("uvSize", frameView->uvSize);
	if (frameView->animateUvOffset)
		serializer.write("uvOffset", frameView->uvOffset);
	if (frameView->animateColorAdd)
		serializer.write("colorAdd", frameView->colorAdd);
	if (frameView->animateColorMul)
		serializer.write("colorMul", frameView->colorMul);
	if (frameView->animateColorMapLayer)
		serializer.write("colorMapLayer", frameView->colorMapLayer);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (frameView->animateColorMap)
	{
		if (!frameView->colorMapPath.empty())
			serializer.write("colorMapPath", frameView->colorMapPath.generic_string());
	}
	#endif
}
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<SpriteAnimFrame>(frame); auto boolValue = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frameView->animateUvSize = deserializer.read("uvSize", frameView->uvSize);
	frameView->animateUvOffset = deserializer.read("uvOffset", frameView->uvOffset);
	frameView->animateColorAdd = deserializer.read("colorAdd", frameView->colorAdd);
	frameView->animateColorMul = deserializer.read("colorMul", frameView->colorMul);
	frameView->animateColorMapLayer = deserializer.read("colorMapLayer", frameView->colorMapLayer);
	frameView->isEnabled = boolValue;

	string colorMapPath;
	if (deserializer.read("colorMapPath", colorMapPath))
	{
		if (colorMapPath.empty()) colorMapPath.assign("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		frameView->colorMapPath = colorMapPath;
		#endif
		frameView->colorMap = ResourceSystem::getInstance()->loadSharedImage(colorMapPath);
		frameView->descriptorSet = {}; // Note: See the imageLoaded()
		frameView->animateColorMap = true;
	}
}
void SpriteRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<SpriteRenderComponent>(component);
	const auto frameA = View<SpriteAnimFrame>(a);
	const auto frameB = View<SpriteAnimFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->isEnabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateUvSize)
		componentView->uvSize = (half2)lerp((float2)frameA->uvSize, (float2)frameB->uvSize, t);
	if (frameA->animateUvOffset)
		componentView->uvOffset = (half2)lerp((float2)frameA->uvOffset, (float2)frameB->uvOffset, t);
	if (frameA->animateColorAdd)
		componentView->colorAdd = lerp(frameA->colorAdd, frameB->colorAdd, t);
	if (frameA->animateColorMul)
		componentView->colorMul = lerp(frameA->colorMul, frameB->colorMul, t);
	if (frameA->animateColorMapLayer)
		componentView->setColorMapLayer(lerp(frameA->colorMapLayer, frameB->colorMapLayer, t));

	if (frameA->animateColorMap)
	{
		if ((bool)round(t))
		{
			if (frameB->descriptorSet)
			{
				componentView->colorMap = frameB->colorMap;
				componentView->descriptorSet = frameB->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				componentView->colorMapPath = frameB->colorMapPath;
				#endif
			}
		}
		else
		{
			if (frameA->descriptorSet)
			{
				componentView->colorMap = frameA->colorMap;
				componentView->descriptorSet = frameA->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				componentView->colorMapPath = frameA->colorMapPath;
				#endif
			}
		}
	}
}
void SpriteRenderSystem::resetAnimation(View<AnimationFrame> frame)
{
	auto resourceSystem = ResourceSystem::getInstance();
	auto frameView = View<SpriteAnimFrame>(frame);
	resourceSystem->destroyShared(frameView->colorMap);
	resourceSystem->destroyShared(frameView->descriptorSet);
}

//**********************************************************************************************************************
Ref<DescriptorSet> SpriteRenderSystem::createSharedDS(string_view path, ID<Image> colorMap)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(colorMap);

	auto colorMapView = GraphicsSystem::getInstance()->get(colorMap);
	auto imageSize = (uint2)colorMapView->getSize();
	auto imageType = colorMapView->getType();

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.data(), path.length());
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));
	Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

	auto uniforms = getSpriteUniforms(colorMapView->getView());
	auto descriptorSet = ResourceSystem::getInstance()->createSharedDS(
		Hash128::digestState(hashState), getBasePipeline(), std::move(uniforms), 1);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + string(path));
	return descriptorSet;
}