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
#include "garden/system/resource.hpp"

using namespace garden;

// TODO: add bindless support

//**********************************************************************************************************************
void SpriteRenderSystem::init()
{
	InstanceRenderSystem::init();
	SUBSCRIBE_TO_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);
}
void SpriteRenderSystem::deinit()
{
	// TODO: somehow destroy default image view

	if (Manager::get()->isRunning())
		UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);

	InstanceRenderSystem::deinit();
}

//**********************************************************************************************************************
void SpriteRenderSystem::imageLoaded()
{
	auto image = ResourceSystem::get()->getLoadedImage();
	const auto& componentPool = getMeshComponentPool();
	auto componentSize = getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto componentOccupnacy = componentPool.getOccupancy();

	// TODO: suboptimal. Use tightly packed sprite arrays (add support of this to the resource system or texture atlases).
	for (uint32 i = 0; i < componentOccupnacy; i++)
	{
		auto spriteRenderView = (SpriteRenderComponent*)(componentData + i * componentSize);
		if (spriteRenderView->colorMap != image)
			continue;

		auto imageView = GraphicsSystem::get()->get(image);
		auto imageSize = imageView->getSize();
		auto imageType = imageView->getType();
		auto uniforms = getSpriteUniforms(imageView->getDefaultView());
		auto name = spriteRenderView->path.generic_string();
		auto hashState = Hash128::createState(); // TODO: maybe cache hash state?
		Hash128::updateState(hashState, name.c_str(), name.length());
		Hash128::updateState(hashState, &imageSize.x, sizeof(int32));
		Hash128::updateState(hashState, &imageSize.y, sizeof(int32));
		Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

		spriteRenderView->descriptorSet = ResourceSystem::get()->createSharedDescriptorSet(
			Hash128::digestState(hashState), getPipeline(), std::move(uniforms), 1);
		Hash128::destroyState(hashState);
	}
}

void SpriteRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<SpriteRenderComponent>(destination);
	const auto sourceView = View<SpriteRenderComponent>(source);

	auto resourceSystem = ResourceSystem::get();
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
	destinationView->path = sourceView->path;
	#endif
}

//**********************************************************************************************************************
bool SpriteRenderSystem::isDrawReady()
{
	if (!InstanceRenderSystem::isDrawReady())
		return false;

	auto graphicsSystem = GraphicsSystem::get();
	auto vertexBufferView = graphicsSystem->get(graphicsSystem->getFullSquareVertices());

	if (!vertexBufferView->isReady())
		return false;

	return true;
}
void SpriteRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto spriteRenderView = (SpriteRenderComponent*)meshRenderView;
	auto instanceData = (InstanceData*)(instanceMap + getInstanceDataSize() * drawIndex);
	setInstanceData(spriteRenderView, instanceData, viewProj, model, drawIndex, taskIndex);

	DescriptorSet::Range descriptorSetRange[8]; uint8 descriptorSetCount = 0;
	setDescriptorSetRange(meshRenderView, descriptorSetRange, descriptorSetCount, 8);
	pipelineView->bindDescriptorSetsAsync(descriptorSetRange, descriptorSetCount, taskIndex);

	auto pushConstantsSize = pipelineView->getPushConstantsSize();
	auto pushConstants = (PushConstants*)(
		pipelineView->getPushConstantsBuffer().data() + pushConstantsSize * taskIndex);
	setPushConstants(spriteRenderView, pushConstants, viewProj, model, drawIndex, taskIndex);
	pipelineView->pushConstantsAsync(taskIndex);

	pipelineView->drawAsync(taskIndex, {}, 6);
}

//**********************************************************************************************************************
uint64 SpriteRenderSystem::getInstanceDataSize()
{
	return (uint64)sizeof(InstanceData);
}
void SpriteRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
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
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
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
	auto whiteTexture = GraphicsSystem::get()->getWhiteTexture();
	if (!defaultImageView)
	{
		auto graphicsSystem = GraphicsSystem::get();
		auto imageView = graphicsSystem->get(whiteTexture);
		defaultImageView = GraphicsSystem::get()->createImageView(
			imageView->getImage(), Image::Type::Texture2DArray);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, defaultImageView, "image.whiteTexture.arrayView");
	}

	map<string, DescriptorSet::Uniform> defaultUniforms =
	{ { "colorMap", DescriptorSet::Uniform(defaultImageView) } };
	return defaultUniforms;
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<SpriteRenderComponent>(component);
	if (componentView->isArray)
		serializer.write("isArray", true);
	if (componentView->aabb != Aabb::one)
		serializer.write("aabb", componentView->aabb);
	if (!componentView->isEnabled)
		serializer.write("isEnabled", false);
	if (componentView->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", componentView->colorMapLayer);
	if (componentView->colorFactor != float4(1.0f))
		serializer.write("colorFactor", componentView->colorFactor);
	if (componentView->uvSize != float2(1.0f))
		serializer.write("uvSize", componentView->uvSize);
	if (componentView->uvOffset != float2(0.0f))
		serializer.write("uvOffset", componentView->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("path", componentView->path.generic_string());
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<SpriteRenderComponent>(component);
	deserializer.read("isArray", componentView->isArray);
	deserializer.read("aabb", componentView->aabb);
	deserializer.read("isEnabled", componentView->isEnabled);
	deserializer.read("colorMapLayer", componentView->colorMapLayer);
	deserializer.read("colorFactor", componentView->colorFactor);
	deserializer.read("uvSize", componentView->uvSize);
	deserializer.read("uvOffset", componentView->uvOffset);

	string path;
	if (deserializer.read("path", path))
	{
		#if GARDEN_DEBUG || GARDEN_EDITOR
		componentView->path = path;
		#endif
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (componentView->path.empty())
		componentView->path = path = "missing";
	#endif

	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (componentView->isArray)
		flags |= ImageLoadFlags::LoadArray;
	componentView->colorMap = ResourceSystem::get()->loadImage(path,
		Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	auto frameView = View<SpriteRenderFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", frameView->isEnabled);
	if (frameView->animateColorFactor)
		serializer.write("colorFactor", frameView->colorFactor);
	if (frameView->animateUvSize)
		serializer.write("uvSize", frameView->uvSize);
	if (frameView->animateUvOffset)
		serializer.write("uvOffset", frameView->uvOffset);
	if (frameView->animateColorMapLayer)
		serializer.write("colorMapLayer", frameView->colorMapLayer);
}
void SpriteRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<SpriteRenderComponent>(component);
	auto frameA = View<SpriteRenderFrame>(a);
	auto frameB = View<SpriteRenderFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->isEnabled = (bool)round(t);
	if (frameA->animateColorFactor)
		componentView->colorFactor = lerp(frameA->colorFactor, frameB->colorFactor, t);
	if (frameA->animateUvSize)
		componentView->uvSize = lerp(frameA->uvSize, frameB->uvSize, t);
	if (frameA->animateUvOffset)
		componentView->uvOffset = lerp(frameA->uvOffset, frameB->uvOffset, t);
	if (frameA->animateColorMapLayer)
		componentView->colorMapLayer = lerp(frameA->colorMapLayer, frameB->colorMapLayer, t);
}
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, SpriteRenderFrame& frame)
{
	frame.animateIsEnabled = deserializer.read("isEnabled", frame.isEnabled);
	frame.animateColorFactor = deserializer.read("colorFactor", frame.colorFactor);
	frame.animateUvSize = deserializer.read("uvSize", frame.uvSize);
	frame.animateUvOffset = deserializer.read("uvOffset", frame.uvOffset);
	frame.animateColorMapLayer = deserializer.read("colorMapLayer", frame.colorMapLayer);
}

void SpriteRenderSystem::tryDestroyResources(View<SpriteRenderComponent> spriteRenderView)
{
	GARDEN_ASSERT(spriteRenderView);
	auto resourceSystem = ResourceSystem::get();
	resourceSystem->destroyShared(spriteRenderView->colorMap);
	resourceSystem->destroyShared(spriteRenderView->descriptorSet);
	spriteRenderView->colorMap = {};
	spriteRenderView->descriptorSet = {};
}