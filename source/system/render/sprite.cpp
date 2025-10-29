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

#include "garden/system/render/sprite.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

// TODO: add bindless support
// TODO: Add automatic tightly packed sprite arrays (add support of this to the resource system or texture atlases).

static constexpr auto imageFlags = ImageLoadFlags::TypeArray | ImageLoadFlags::LoadShared;
static constexpr auto imageUsage = Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ;

SpriteRenderSystem::SpriteRenderSystem(const fs::path& pipelinePath) : pipelinePath(pipelinePath) { }

void SpriteRenderSystem::init()
{
	InstanceRenderSystem::init();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);
	

	#if GARDEN_DEBUG
	debugResourceName = pipelinePath.generic_string();
	#endif
}
void SpriteRenderSystem::deinit()
{
	// TODO: somehow destroy default image view, maybe check it ref count?

	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);

	InstanceRenderSystem::deinit();
}

//**********************************************************************************************************************
void SpriteRenderSystem::imageLoaded()
{
	auto resourceSystem = ResourceSystem::Instance::get();
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
	auto resourceSystem = ResourceSystem::Instance::get();
	auto componentView = View<SpriteRenderComponent>(component);
	resourceSystem->destroyShared(componentView->colorMap);
	resourceSystem->destroyShared(componentView->descriptorSet);
	componentView->colorMap = {}; componentView->descriptorSet = {};
}

//**********************************************************************************************************************
void SpriteRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;
	if (!spriteRenderView->descriptorSet)
		return;

	DescriptorSet::Range dsRange[2];
	dsRange[0] = DescriptorSet::Range(descriptorSet, 1, inFlightIndex);
	dsRange[1] = DescriptorSet::Range((ID<DescriptorSet>)spriteRenderView->descriptorSet);

	auto instanceData = (BaseInstanceData*)(instanceMap + drawIndex * getBaseInstanceDataSize());
	setInstanceData(spriteRenderView, instanceData, viewProj, model, drawIndex, taskIndex);

	PushConstants pc;
	setPushConstants(spriteRenderView, &pc, viewProj, model, drawIndex, taskIndex);

	pipelineView->bindDescriptorSetsAsync(dsRange, 2, taskIndex);
	pipelineView->pushConstantsAsync(&pc, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6);
}

uint64 SpriteRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(BaseInstanceData);
}
void SpriteRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	instanceData->mvp = (float4x4)(viewProj * model);
	instanceData->color = (float4)srgbToRgb(spriteRenderView->color);
	instanceData->uvSize = spriteRenderView->uvSize;
	instanceData->uvOffset = spriteRenderView->uvOffset;
}
void SpriteRenderSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	pushConstants->instanceIndex = drawIndex;
	pushConstants->colorMapLayer = spriteRenderView->colorMapLayer;
}

//**********************************************************************************************************************
DescriptorSet::Uniforms SpriteRenderSystem::getSpriteUniforms(ID<ImageView> colorMap)
{
	DescriptorSet::Uniforms spriteUniforms = { { "colorMap", DescriptorSet::Uniform(colorMap) } };
	return spriteUniforms;
}
ID<GraphicsPipeline> SpriteRenderSystem::createBasePipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::tryGet();
	ID<Framebuffer> framebuffer; GraphicsPipeline::State pipelineState;
	if (deferredSystem)
	{
		if (getMeshRenderType() == MeshRenderType::UI)
		{
			framebuffer = deferredSystem->getUiFramebuffer();
		}
		else
		{
			framebuffer = deferredSystem->getDepthHdrFramebuffer();
			pipelineState.depthTesting = pipelineState.depthWriting = true;
		}
	}
	else
	{
		framebuffer = ForwardRenderSystem::Instance::get()->getColorFramebuffer();
	}
	GraphicsPipeline::PipelineStates pipelineStates = { { 0, pipelineState } };

	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = true;
	options.loadAsync = false; // We can't load async due to imageLoaded() usage.
	options.pipelineStateOverrides = &pipelineStates;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(pipelinePath, framebuffer, options);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<SpriteRenderComponent>(component);
	if (componentView->isArray)
		serializer.write("isArray", true);
	if (componentView->useMipmap)
		serializer.write("useMipmap", true);
	if (componentView->aabb != Aabb::one)
		serializer.write("aabb", componentView->aabb);
	if (!componentView->isEnabled)
		serializer.write("isEnabled", false);
	if (componentView->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", componentView->colorMapLayer);
	if (componentView->color != f32x4::one)
		serializer.write("color", (float4)componentView->color);
	if (componentView->uvSize != float2::one)
		serializer.write("uvSize", componentView->uvSize);
	if (componentView->uvOffset != float2::zero)
		serializer.write("uvOffset", componentView->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!componentView->colorMapPath.empty())
		serializer.write("colorMapPath", componentView->colorMapPath.generic_string());
	if (componentView->taskPriority != 0.0f)
		serializer.write("taskPriority", componentView->taskPriority);
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<SpriteRenderComponent>(component);
	deserializer.read("isArray", componentView->isArray);
	deserializer.read("useMipmap", componentView->useMipmap);
	deserializer.read("aabb", componentView->aabb);
	deserializer.read("isEnabled", componentView->isEnabled);
	deserializer.read("colorMapLayer", componentView->colorMapLayer);
	deserializer.read("color", componentView->color);
	deserializer.read("uvSize", componentView->uvSize);
	deserializer.read("uvOffset", componentView->uvOffset);

	string colorMapPath;
	deserializer.read("colorMapPath", colorMapPath);
	if (colorMapPath.empty())
		colorMapPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	componentView->colorMapPath = colorMapPath;
	#endif

	auto taskPriority = 0.0f;
	deserializer.read("taskPriority", taskPriority);

	auto maxMipCount = componentView->useMipmap ? 0 : 1;
	auto flags = imageFlags; if (componentView->isArray) flags |= ImageLoadFlags::LoadArray;
	auto usage = imageUsage; if (maxMipCount == 0) usage |= Image::Usage::TransferSrc;
	componentView->colorMap = ResourceSystem::Instance::get()->loadImage(
		colorMapPath, usage, maxMipCount, Image::Strategy::Default, flags, taskPriority);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<SpriteAnimFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
	if (frameView->animateColor)
		serializer.write("color", (float4)frameView->color);
	if (frameView->animateUvSize)
		serializer.write("uvSize", frameView->uvSize);
	if (frameView->animateUvOffset)
		serializer.write("uvOffset", frameView->uvOffset);
	if (frameView->animateColorMapLayer)
		serializer.write("colorMapLayer", frameView->colorMapLayer);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (frameView->animateColorMap)
	{
		if (!frameView->colorMapPath.empty())
			serializer.write("colorMapPath", frameView->colorMapPath.generic_string());
		if (frameView->taskPriority != 0.0f)
			serializer.write("taskPriority", frameView->taskPriority);
		if (frameView->isArray)
			serializer.write("isArray", true);
		if (frameView->useMipmap)
			serializer.write("useMipmap", true);
	}
	#endif
}
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<SpriteAnimFrame>(frame); auto boolValue = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frameView->animateColor = deserializer.read("color", frameView->color);
	frameView->animateUvSize = deserializer.read("uvSize", frameView->uvSize);
	frameView->animateUvOffset = deserializer.read("uvOffset", frameView->uvOffset);
	frameView->animateColorMapLayer = deserializer.read("colorMapLayer", frameView->colorMapLayer);
	frameView->isEnabled = boolValue;

	string colorMapPath;
	frameView->animateColorMap = deserializer.read("colorMapPath", colorMapPath);
	if (colorMapPath.empty())
		colorMapPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frameView->colorMapPath = colorMapPath;
	#endif

	boolValue = false;
	deserializer.read("isArray", boolValue);
	frameView->isArray = boolValue;

	auto taskPriority = 0.0f;
	deserializer.read("taskPriority", taskPriority);

	auto maxMipCount = frameView->useMipmap ? 0 : 1;
	auto flags = imageFlags; if (frameView->isArray) flags |= ImageLoadFlags::LoadArray;
	auto usage = imageUsage; if (maxMipCount == 0) usage |= Image::Usage::TransferSrc;
	frameView->colorMap = ResourceSystem::Instance::get()->loadImage(colorMapPath, 
		usage, maxMipCount, Image::Strategy::Default, flags, taskPriority);
	frameView->descriptorSet = {}; // Note: See the imageLoaded()
}
void SpriteRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<SpriteRenderComponent>(component);
	const auto frameA = View<SpriteAnimFrame>(a);
	const auto frameB = View<SpriteAnimFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->isEnabled = (bool)round(t);
	if (frameA->animateColor)
		componentView->color = lerp(frameA->color, frameB->color, t);
	if (frameA->animateUvSize)
		componentView->uvSize = lerp(frameA->uvSize, frameB->uvSize, t);
	if (frameA->animateUvOffset)
		componentView->uvOffset = lerp(frameA->uvOffset, frameB->uvOffset, t);
	if (frameA->animateColorMapLayer)
		componentView->colorMapLayer = lerp(frameA->colorMapLayer, frameB->colorMapLayer, t);

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
				componentView->isArray = frameB->isArray;
				componentView->useMipmap = frameB->useMipmap;
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
				componentView->isArray = frameA->isArray;
				componentView->useMipmap = frameA->useMipmap;
			}
		}
	}
}
void SpriteRenderSystem::resetAnimation(View<AnimationFrame> frame)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto frameView = View<SpriteAnimFrame>(frame);
	resourceSystem->destroyShared(frameView->colorMap);
	resourceSystem->destroyShared(frameView->descriptorSet);
	frameView->colorMap = {}; frameView->descriptorSet = {};
}

//**********************************************************************************************************************
Ref<DescriptorSet> SpriteRenderSystem::createSharedDS(string_view path, ID<Image> colorMap)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(colorMap);

	auto colorMapView = GraphicsSystem::Instance::get()->get(colorMap);
	auto imageSize = (uint2)colorMapView->getSize();
	auto imageType = colorMapView->getType();

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.data(), path.length());
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));
	Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

	auto uniforms = getSpriteUniforms(colorMapView->getDefaultView());
	auto descriptorSet = ResourceSystem::Instance::get()->createSharedDS(
		Hash128::digestState(hashState), getBasePipeline(), std::move(uniforms), 1);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + string(path));
	return descriptorSet;
}