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

	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);
}
void SpriteRenderSystem::deinit()
{
	// TODO: somehow destroy default image view

	auto manager = Manager::getInstance();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);

	InstanceRenderSystem::deinit();
}

//**********************************************************************************************************************
void SpriteRenderSystem::imageLoaded()
{
	auto image = ResourceSystem::getInstance()->getLoadedImage();
	const auto& componentPool = getMeshComponentPool();
	auto componentSize = getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto componentOccupnacy = componentPool.getOccupancy();

	// TODO: suboptimal. Use tightly packed sprite arrays (add support of this to the resource system or texture atlases).
	for (uint32 i = 0; i < componentOccupnacy; i++)
	{
		auto spriteRender = (SpriteRenderComponent*)(componentData + i * componentSize);
		if (spriteRender->colorMap != image)
			continue;

		auto imageView = GraphicsSystem::getInstance()->get(image);
		auto imageSize = imageView->getSize();
		auto name = spriteRender->path; name += ";";
		name += to_string(imageSize.x); name += ";";
		name += to_string(imageSize.y); name += ";";
		name += to_string((uint8)imageView->getType());
		auto uniforms = getSpriteUniforms(imageView->getDefaultView());

		spriteRender->descriptorSet = ResourceSystem::getInstance()->createSharedDescriptorSet(
			name, getPipeline(), std::move(uniforms), 1);
	}
}

void SpriteRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<SpriteRenderComponent>(destination);
	const auto sourceView = View<SpriteRenderComponent>(source);

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

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto vertexBufferView = graphicsSystem->get(graphicsSystem->getFullSquareVertices());

	if (!vertexBufferView->isReady())
		return false;

	return true;
}
void SpriteRenderSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto spriteRenderComponent = (SpriteRenderComponent*)meshRenderComponent;
	auto instanceData = (InstanceData*)(instanceMap + getInstanceDataSize() * drawIndex);
	setInstanceData(spriteRenderComponent, instanceData, viewProj, model, drawIndex, taskIndex);

	DescriptorSet::Range descriptorSetRange[8]; uint8 descriptorSetCount = 0;
	setDescriptorSetRange(meshRenderComponent, descriptorSetRange, descriptorSetCount, 8);
	pipelineView->bindDescriptorSetsAsync(descriptorSetRange, descriptorSetCount, taskIndex);

	auto pushConstantsSize = pipelineView->getPushConstantsSize();
	auto pushConstants = (PushConstants*)(
		pipelineView->getPushConstantsBuffer().data() + pushConstantsSize * taskIndex);
	setPushConstants(spriteRenderComponent, pushConstants, viewProj, model, drawIndex, taskIndex);
	pipelineView->pushConstantsAsync(taskIndex);

	pipelineView->drawAsync(taskIndex, {}, 6);
}

//**********************************************************************************************************************
uint64 SpriteRenderSystem::getInstanceDataSize()
{
	return (uint64)sizeof(InstanceData);
}
void SpriteRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderComponent, InstanceData* instanceData,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	instanceData->mvp = viewProj * model;
	instanceData->colorFactor = spriteRenderComponent->colorFactor;
	instanceData->sizeOffset = float4(spriteRenderComponent->uvSize, spriteRenderComponent->uvOffset);
}
void SpriteRenderSystem::setDescriptorSetRange(MeshRenderComponent* meshRenderComponent,
	DescriptorSet::Range* range, uint8& index, uint8 capacity)
{
	InstanceRenderSystem::setDescriptorSetRange(meshRenderComponent, range, index, capacity);

	GARDEN_ASSERT(index < capacity);
	auto spriteRenderComponent = (SpriteRenderComponent*)meshRenderComponent;
	range[index++] = DescriptorSet::Range(spriteRenderComponent->descriptorSet ?
		(ID<DescriptorSet>)spriteRenderComponent->descriptorSet : defaultDescriptorSet);
}
void SpriteRenderSystem::setPushConstants(SpriteRenderComponent* spriteRenderComponent, PushConstants* pushConstants, 
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	pushConstants->instanceIndex = drawIndex;
	pushConstants->colorMapLayer = spriteRenderComponent->colorMapLayer;
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
	auto whiteTexture = GraphicsSystem::getInstance()->getWhiteTexture();
	if (!defaultImageView)
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		auto imageView = graphicsSystem->get(whiteTexture);
		defaultImageView = GraphicsSystem::getInstance()->createImageView(
			imageView->getImage(), Image::Type::Texture2DArray);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, defaultImageView, "image.whiteTexture.arrayView");
	}

	map<string, DescriptorSet::Uniform> defaultUniforms =
	{ { "colorMap", DescriptorSet::Uniform(defaultImageView) } };
	return defaultUniforms;
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<SpriteRenderComponent>(component);
	if (componentView->isArray != false)
		serializer.write("isArray", componentView->isArray);
	if (componentView->aabb != Aabb::one)
		serializer.write("aabb", componentView->aabb);
	if (componentView->isEnabled != true)
		serializer.write("isEnabled", componentView->isEnabled);
	if (componentView->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", componentView->colorMapLayer);
	if (componentView->colorFactor != float4(1.0f))
		serializer.write("colorFactor", componentView->colorFactor);
	if (componentView->uvSize != float2(1.0f))
		serializer.write("uvSize", componentView->uvSize);
	if (componentView->uvOffset != float2(0.0f))
		serializer.write("uvOffset", componentView->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("path", componentView->path);
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
	deserializer.read("path", componentView->path);

	if (componentView->path.empty())
		componentView->path = "missing";
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (componentView->isArray)
		flags |= ImageLoadFlags::LoadArray;
	componentView->colorMap = ResourceSystem::getInstance()->loadImage(componentView->path,
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

void SpriteRenderSystem::tryDestroyResources(View<SpriteRenderComponent> spriteComponent)
{
	GARDEN_ASSERT(spriteComponent);
	auto resourceSystem = ResourceSystem::getInstance();
	resourceSystem->destroyShared(spriteComponent->colorMap);
	resourceSystem->destroyShared(spriteComponent->descriptorSet);
	spriteComponent->colorMap = {};
	spriteComponent->descriptorSet = {};
}