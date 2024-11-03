// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

//**********************************************************************************************************************
SpriteRenderSystem::SpriteRenderSystem(const fs::path& pipelinePath, 
	bool useDeferredBuffer, bool useLinearFilter, bool isTranslucent) : 
	useDeferredBuffer(useDeferredBuffer), useLinearFilter(useLinearFilter),
	isTranslucent(isTranslucent), pipelinePath(pipelinePath) { }

void SpriteRenderSystem::init()
{
	InstanceRenderSystem::init();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);

	#if GARDEN_DEBUG
	setInstancesBuffersName(pipelinePath.generic_string());
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
		auto spriteRenderFrame = (SpriteAnimationFrame*)(componentData + i * componentSize);
		if (spriteRenderFrame->colorMap != image || spriteRenderFrame->descriptorSet)
			continue;
		if (!descriptorSet)
			descriptorSet = createSharedDS(imagePath.generic_string(), image);
		spriteRenderFrame->descriptorSet = descriptorSet;
	}
}

//**********************************************************************************************************************
void SpriteRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<SpriteRenderComponent>(destination);
	const auto sourceView = View<SpriteRenderComponent>(source);

	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(destinationView->descriptorSet);
	resourceSystem->destroyShared(destinationView->colorMap);

	destinationView->aabb = sourceView->aabb;
	destinationView->isEnabled = sourceView->isEnabled;
	destinationView->isArray = sourceView->isArray;
	destinationView->colorMap = sourceView->colorMap;
	destinationView->descriptorSet = sourceView->descriptorSet;
	destinationView->colorMapLayer = sourceView->colorMapLayer;
	destinationView->colorFactor = sourceView->colorFactor;
	destinationView->uvSize = sourceView->uvSize;
	destinationView->uvOffset = sourceView->uvOffset;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->colorMapPath = sourceView->colorMapPath;
	#endif
}

//**********************************************************************************************************************
bool SpriteRenderSystem::isDrawReady()
{
	if (!InstanceRenderSystem::isDrawReady())
		return false;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto vertexBufferView = graphicsSystem->get(graphicsSystem->getFullSquareVertices());

	if (!vertexBufferView->isReady())
		return false;

	return true;
}
void SpriteRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 threadIndex)
{
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;
	auto instanceData = (InstanceData*)(instanceMap + getInstanceDataSize() * drawIndex);
	setInstanceData(spriteRenderView, instanceData, viewProj, model, drawIndex, threadIndex);

	DescriptorSet::Range descriptorSetRange[8]; uint8 descriptorSetCount = 0;
	setDescriptorSetRange(meshRenderView, descriptorSetRange, descriptorSetCount, 8);
	pipelineView->bindDescriptorSetsAsync(descriptorSetRange, descriptorSetCount, threadIndex);

	auto pushConstants = (PushConstants*)pipelineView->getPushConstantsAsync(threadIndex);
	setPushConstants(spriteRenderView, pushConstants, viewProj, model, drawIndex, threadIndex);
	pipelineView->pushConstantsAsync(threadIndex);

	pipelineView->drawAsync(threadIndex, {}, 6);
}

//**********************************************************************************************************************
uint64 SpriteRenderSystem::getInstanceDataSize()
{
	return (uint64)sizeof(InstanceData);
}
void SpriteRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 threadIndex)
{
	instanceData->mvp = viewProj * model;
	instanceData->colorFactor = spriteRenderView->colorFactor;
	instanceData->sizeOffset = float4(spriteRenderView->uvSize, spriteRenderView->uvOffset);
}
void SpriteRenderSystem::setDescriptorSetRange(MeshRenderComponent* meshRenderView,
	DescriptorSet::Range* range, uint8& index, uint8 capacity)
{
	InstanceRenderSystem::setDescriptorSetRange(meshRenderView, range, index, capacity);

	GARDEN_ASSERT(index < capacity);
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;
	range[index++] = DescriptorSet::Range(spriteRenderView->descriptorSet ?
		(ID<DescriptorSet>)spriteRenderView->descriptorSet : defaultDescriptorSet);
}
void SpriteRenderSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 threadIndex)
{
	pushConstants->instanceIndex = drawIndex;
	pushConstants->colorMapLayer = spriteRenderView->colorMapLayer;
}

//**********************************************************************************************************************
map<string, DescriptorSet::Uniform> SpriteRenderSystem::getSpriteUniforms(ID<ImageView> colorMap)
{
	map<string, DescriptorSet::Uniform> spriteUniforms =
	{ { "colorMap", DescriptorSet::Uniform(colorMap) } };
	return spriteUniforms;
}
map<string, DescriptorSet::Uniform> SpriteRenderSystem::getDefaultUniforms()
{
	auto whiteTexture = GraphicsSystem::Instance::get()->getWhiteTexture();
	if (!defaultImageView)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto imageView = graphicsSystem->get(whiteTexture);
		defaultImageView = graphicsSystem->createImageView(
			imageView->getImage(), Image::Type::Texture2DArray);
		SET_RESOURCE_DEBUG_NAME(defaultImageView, 
			"image.whiteTexture.arrayView." + pipelinePath.generic_string());
	}

	map<string, DescriptorSet::Uniform> defaultUniforms =
	{ { "colorMap", DescriptorSet::Uniform(defaultImageView) } };
	return defaultUniforms;
}
ID<GraphicsPipeline> SpriteRenderSystem::createPipeline()
{
	ID<Framebuffer> framebuffer;
	if (useDeferredBuffer)
	{
		auto deferredSystem = DeferredRenderSystem::Instance::get();
		framebuffer = isTranslucent ? deferredSystem->getTranslucentFramebuffer() : 
			deferredSystem->getGFramebuffer();
	}
	else
	{
		framebuffer = ForwardRenderSystem::Instance::get()->getFramebuffer();
	}

	GraphicsPipeline::StateOverrides stateOverrides;
	GraphicsPipeline::StateOverrides* stateOverridesPtr = nullptr;
	if (!useLinearFilter)
	{
		Pipeline::SamplerState samplerState;
		samplerState.wrapX = samplerState.wrapY = samplerState.wrapZ =
			GraphicsPipeline::SamplerWrap::Repeat;
		stateOverrides.samplerStates.emplace("colorMap", samplerState);
		stateOverridesPtr = &stateOverrides;
	}

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(pipelinePath,
		framebuffer, true, true, 0, 0, {}, stateOverridesPtr);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	if (spriteRenderView->isArray)
		serializer.write("isArray", true);
	if (spriteRenderView->aabb != Aabb::one)
		serializer.write("aabb", spriteRenderView->aabb);
	if (!spriteRenderView->isEnabled)
		serializer.write("isEnabled", false);
	if (spriteRenderView->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", spriteRenderView->colorMapLayer);
	if (spriteRenderView->colorFactor != float4(1.0f))
		serializer.write("colorFactor", spriteRenderView->colorFactor);
	if (spriteRenderView->uvSize != float2(1.0f))
		serializer.write("uvSize", spriteRenderView->uvSize);
	if (spriteRenderView->uvOffset != float2(0.0f))
		serializer.write("uvOffset", spriteRenderView->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("colorMapPath", spriteRenderView->colorMapPath.generic_string());
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	deserializer.read("isArray", spriteRenderView->isArray);
	deserializer.read("aabb", spriteRenderView->aabb);
	deserializer.read("isEnabled", spriteRenderView->isEnabled);
	deserializer.read("colorMapLayer", spriteRenderView->colorMapLayer);
	deserializer.read("colorFactor", spriteRenderView->colorFactor);
	deserializer.read("uvSize", spriteRenderView->uvSize);
	deserializer.read("uvOffset", spriteRenderView->uvOffset);

	string colorMapPath;
	deserializer.read("colorMapPath", colorMapPath);
	#if GARDEN_DEBUG || GARDEN_EDITOR
	spriteRenderView->colorMapPath = colorMapPath;
	#endif

	if (colorMapPath.empty())
	{
		colorMapPath = "missing";
		#if GARDEN_DEBUG || GARDEN_EDITOR
		spriteRenderView->colorMapPath = "missing";
		#endif
	}

	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (spriteRenderView->isArray)
		flags |= ImageLoadFlags::LoadArray;
	spriteRenderView->colorMap = ResourceSystem::Instance::get()->loadImage(colorMapPath,
		Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	auto spriteFrameView = View<SpriteAnimationFrame>(frame);
	if (spriteFrameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)spriteFrameView->isEnabled);
	if (spriteFrameView->animateColorFactor)
		serializer.write("colorFactor", spriteFrameView->colorFactor);
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
		if (spriteFrameView->isArray)
			serializer.write("isArray", true);
	}
	#endif
}
void SpriteRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto spriteRenderView = View<SpriteRenderComponent>(component);
	auto frameA = View<SpriteAnimationFrame>(a);
	auto frameB = View<SpriteAnimationFrame>(b);

	if (frameA->animateIsEnabled)
		spriteRenderView->isEnabled = (bool)round(t);
	if (frameA->animateColorFactor)
		spriteRenderView->colorFactor = lerp(frameA->colorFactor, frameB->colorFactor, t);
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
				spriteRenderView->isArray = frameB->isArray;
				spriteRenderView->colorMap = frameB->colorMap;
				spriteRenderView->descriptorSet = frameB->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				spriteRenderView->colorMapPath = frameB->colorMapPath;
				#endif
			}
		}
		else
		{
			if (frameA->descriptorSet)
			{
				spriteRenderView->isArray = frameA->isArray;
				spriteRenderView->colorMap = frameA->colorMap;
				spriteRenderView->descriptorSet = frameA->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				spriteRenderView->colorMapPath = frameA->colorMapPath;
				#endif
			}
		}
	}
}
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, SpriteAnimationFrame& frame)
{
	auto boolValue = true;
	frame.animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frame.isEnabled = boolValue;
	frame.animateColorFactor = deserializer.read("colorFactor", frame.colorFactor);
	frame.animateUvSize = deserializer.read("uvSize", frame.uvSize);
	frame.animateUvOffset = deserializer.read("uvOffset", frame.uvOffset);
	frame.animateColorMapLayer = deserializer.read("colorMapLayer", frame.colorMapLayer);

	string colorMapPath;
	frame.animateColorMap = deserializer.read("colorMapPath", colorMapPath);
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.colorMapPath = colorMapPath;
	#endif

	if (colorMapPath.empty())
	{
		colorMapPath = "missing";
		#if GARDEN_DEBUG || GARDEN_EDITOR
		frame.colorMapPath = "missing";
		#endif
	}

	boolValue = false;
	deserializer.read("isArray", boolValue);
	frame.isArray = boolValue;

	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (frame.isArray)
		flags |= ImageLoadFlags::LoadArray;
	frame.colorMap = ResourceSystem::Instance::get()->loadImage(colorMapPath,
		Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
	frame.descriptorSet = {}; // See the imageLoaded()
}

//**********************************************************************************************************************
void SpriteRenderSystem::destroyResources(View<SpriteAnimationFrame> frameView)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(frameView->colorMap);
	resourceSystem->destroyShared(frameView->descriptorSet);
	frameView->colorMap = {};
	frameView->descriptorSet = {};
}
void SpriteRenderSystem::destroyResources(View<SpriteRenderComponent> spriteRenderView)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(spriteRenderView->colorMap);
	resourceSystem->destroyShared(spriteRenderView->descriptorSet);
	spriteRenderView->colorMap = {};
	spriteRenderView->descriptorSet = {};
}

Ref<DescriptorSet> SpriteRenderSystem::createSharedDS(const string& path, ID<Image> image)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(image);

	auto imageView = GraphicsSystem::Instance::get()->get(image);
	auto imageSize = (uint2)imageView->getSize();
	auto imageType = imageView->getType();

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.c_str(), path.length());
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));
	Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

	auto uniforms = getSpriteUniforms(imageView->getDefaultView());
	auto descriptorSet = ResourceSystem::Instance::get()->createSharedDS(
		Hash128::digestState(hashState), getPipeline(), std::move(uniforms), 1);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptoSet.shared." + path);
	return descriptorSet;
}