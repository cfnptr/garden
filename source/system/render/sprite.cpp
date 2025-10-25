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

	auto& spriteFramePool = getAnimationFramePool();
	componentSize = getAnimationFrameSize();
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

void SpriteRenderSystem::resetComponent(View<Component> component, bool full)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	resourceSystem->destroyShared(spriteRenderView->colorMap);
	resourceSystem->destroyShared(spriteRenderView->descriptorSet);
	spriteRenderView->colorMap = {}; spriteRenderView->descriptorSet = {};

	if (full)
	{
		spriteRenderView->isEnabled = true;
		spriteRenderView->aabb = Aabb::one;
		spriteRenderView->color = f32x4::one;
		spriteRenderView->uvSize = float2::one;
		spriteRenderView->uvOffset = float2::zero;
		#if GARDEN_DEBUG || GARDEN_EDITOR
		spriteRenderView->colorMapPath = "";
		spriteRenderView->taskPriority = 0.0f;
		#endif
		spriteRenderView->colorMapLayer = 0.0f;
		spriteRenderView->isArray = false;
		spriteRenderView->useMipmap = false;
	}
}
void SpriteRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<SpriteRenderComponent>(destination);
	const auto sourceView = View<SpriteRenderComponent>(source);

	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(destinationView->descriptorSet);
	resourceSystem->destroyShared(destinationView->colorMap);

	destinationView->isEnabled = sourceView->isEnabled;
	destinationView->aabb = sourceView->aabb;
	destinationView->color = sourceView->color;
	destinationView->colorMap = sourceView->colorMap;
	destinationView->descriptorSet = sourceView->descriptorSet;
	destinationView->uvSize = sourceView->uvSize;
	destinationView->uvOffset = sourceView->uvOffset;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->colorMapPath = sourceView->colorMapPath;
	destinationView->taskPriority = sourceView->taskPriority;
	#endif
	destinationView->colorMapLayer = sourceView->colorMapLayer;
	destinationView->isArray = sourceView->isArray;
	destinationView->useMipmap = sourceView->useMipmap;
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
	const auto spriteRenderView = View<SpriteRenderComponent>(component);
	if (spriteRenderView->isArray)
		serializer.write("isArray", true);
	if (spriteRenderView->useMipmap)
		serializer.write("useMipmap", true);
	if (spriteRenderView->aabb != Aabb::one)
		serializer.write("aabb", spriteRenderView->aabb);
	if (!spriteRenderView->isEnabled)
		serializer.write("isEnabled", false);
	if (spriteRenderView->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", spriteRenderView->colorMapLayer);
	if (spriteRenderView->color != f32x4::one)
		serializer.write("color", (float4)spriteRenderView->color);
	if (spriteRenderView->uvSize != float2::one)
		serializer.write("uvSize", spriteRenderView->uvSize);
	if (spriteRenderView->uvOffset != float2::zero)
		serializer.write("uvOffset", spriteRenderView->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!spriteRenderView->colorMapPath.empty())
		serializer.write("colorMapPath", spriteRenderView->colorMapPath.generic_string());
	if (spriteRenderView->taskPriority != 0.0f)
		serializer.write("taskPriority", spriteRenderView->taskPriority);
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	deserializer.read("isArray", spriteRenderView->isArray);
	deserializer.read("useMipmap", spriteRenderView->useMipmap);
	deserializer.read("aabb", spriteRenderView->aabb);
	deserializer.read("isEnabled", spriteRenderView->isEnabled);
	deserializer.read("colorMapLayer", spriteRenderView->colorMapLayer);
	deserializer.read("color", spriteRenderView->color);
	deserializer.read("uvSize", spriteRenderView->uvSize);
	deserializer.read("uvOffset", spriteRenderView->uvOffset);

	string colorMapPath;
	deserializer.read("colorMapPath", colorMapPath);
	if (colorMapPath.empty())
		colorMapPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	spriteRenderView->colorMapPath = colorMapPath;
	#endif

	float taskPriority = 0.0f;
	deserializer.read("taskPriority", taskPriority);

	auto maxMipCount = spriteRenderView->useMipmap ? 0 : 1;
	auto flags = imageFlags; if (spriteRenderView->isArray) flags |= ImageLoadFlags::LoadArray;
	auto usage = imageUsage; if (maxMipCount == 0) usage |= Image::Usage::TransferSrc;
	spriteRenderView->colorMap = ResourceSystem::Instance::get()->loadImage(
		colorMapPath, usage, maxMipCount, Image::Strategy::Default, flags, taskPriority);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto spriteFrameView = View<SpriteAnimFrame>(frame);
	if (spriteFrameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)spriteFrameView->isEnabled);
	if (spriteFrameView->animateColor)
		serializer.write("color", (float4)spriteFrameView->color);
	if (spriteFrameView->animateUvSize)
		serializer.write("uvSize", spriteFrameView->uvSize);
	if (spriteFrameView->animateUvOffset)
		serializer.write("uvOffset", spriteFrameView->uvOffset);
	if (spriteFrameView->animateColorMapLayer)
		serializer.write("colorMapLayer", spriteFrameView->colorMapLayer);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (spriteFrameView->animateColorMap)
	{
		if (!spriteFrameView->colorMapPath.empty())
			serializer.write("colorMapPath", spriteFrameView->colorMapPath.generic_string());
		if (spriteFrameView->taskPriority != 0.0f)
			serializer.write("taskPriority", spriteFrameView->taskPriority);
		if (spriteFrameView->isArray)
			serializer.write("isArray", true);
		if (spriteFrameView->useMipmap)
			serializer.write("useMipmap", true);
	}
	#endif
}
void SpriteRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	const auto frameA = View<SpriteAnimFrame>(a);
	const auto frameB = View<SpriteAnimFrame>(b);

	if (frameA->animateIsEnabled)
		spriteRenderView->isEnabled = (bool)round(t);
	if (frameA->animateColor)
		spriteRenderView->color = lerp(frameA->color, frameB->color, t);
	if (frameA->animateUvSize)
		spriteRenderView->uvSize = lerp(frameA->uvSize, frameB->uvSize, t);
	if (frameA->animateUvOffset)
		spriteRenderView->uvOffset = lerp(frameA->uvOffset, frameB->uvOffset, t);
	if (frameA->animateColorMapLayer)
		spriteRenderView->colorMapLayer = lerp(frameA->colorMapLayer, frameB->colorMapLayer, t);

	if (frameA->animateColorMap)
	{
		if (round(t) > 0.0f)
		{
			if (frameB->descriptorSet)
			{
				spriteRenderView->colorMap = frameB->colorMap;
				spriteRenderView->descriptorSet = frameB->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				spriteRenderView->colorMapPath = frameB->colorMapPath;
				#endif
				spriteRenderView->isArray = frameB->isArray;
				spriteRenderView->useMipmap = frameB->useMipmap;
			}
		}
		else
		{
			if (frameA->descriptorSet)
			{
				spriteRenderView->colorMap = frameA->colorMap;
				spriteRenderView->descriptorSet = frameA->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				spriteRenderView->colorMapPath = frameA->colorMapPath;
				#endif
				spriteRenderView->isArray = frameA->isArray;
				spriteRenderView->useMipmap = frameA->useMipmap;
			}
		}
	}
}
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, SpriteAnimFrame& frame)
{
	auto boolValue = true;
	frame.animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frame.isEnabled = boolValue;
	frame.animateColor = deserializer.read("color", frame.color);
	frame.animateUvSize = deserializer.read("uvSize", frame.uvSize);
	frame.animateUvOffset = deserializer.read("uvOffset", frame.uvOffset);
	frame.animateColorMapLayer = deserializer.read("colorMapLayer", frame.colorMapLayer);

	string colorMapPath;
	frame.animateColorMap = deserializer.read("colorMapPath", colorMapPath);
	if (colorMapPath.empty())
		colorMapPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.colorMapPath = colorMapPath;
	#endif

	boolValue = false;
	deserializer.read("isArray", boolValue);
	frame.isArray = boolValue;

	float taskPriority = 0.0f;
	deserializer.read("taskPriority", taskPriority);

	auto maxMipCount = frame.useMipmap ? 0 : 1;
	auto flags = imageFlags; if (frame.isArray) flags |= ImageLoadFlags::LoadArray;
	auto usage = imageUsage; if (maxMipCount == 0) usage |= Image::Usage::TransferSrc;
	frame.colorMap = ResourceSystem::Instance::get()->loadImage(colorMapPath, 
		usage, maxMipCount, Image::Strategy::Default, flags, taskPriority);
	frame.descriptorSet = {}; // Note: See the imageLoaded()
}

void SpriteRenderSystem::resetAnimation(View<AnimationFrame> frame, bool full)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto spriteFrameView = View<SpriteAnimFrame>(frame);
	resourceSystem->destroyShared(spriteFrameView->colorMap);
	resourceSystem->destroyShared(spriteFrameView->descriptorSet);
	spriteFrameView->colorMap = {}; spriteFrameView->descriptorSet = {};
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