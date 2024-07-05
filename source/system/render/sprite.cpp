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
	InstanceData instanceData;
	instanceData.mvp = viewProj * model;
	instanceData.colorFactor = spriteRenderComponent->colorFactor;
	instanceData.sizeOffset = float4(spriteRenderComponent->uvSize, spriteRenderComponent->uvOffset);
	((InstanceData*)instanceMap)[drawIndex] = instanceData;

	DescriptorSet::Range descriptorSetRange[8]; uint8 descriptorSetCount = 0;
	setDescriptorSetRange(meshRenderComponent, descriptorSetRange, descriptorSetCount, 8);
	pipelineView->bindDescriptorSetsAsync(descriptorSetRange, descriptorSetCount, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6);
}

//**********************************************************************************************************************
void SpriteRenderSystem::setDescriptorSetRange(MeshRenderComponent* meshRenderComponent,
	DescriptorSet::Range* range, uint8& index, uint8 capacity)
{
	InstanceRenderSystem::setDescriptorSetRange(meshRenderComponent, range, index, capacity);
	GARDEN_ASSERT(index < capacity);

	auto spriteRenderComponent = (SpriteRenderComponent*)meshRenderComponent;
	range[index++] = DescriptorSet::Range(spriteRenderComponent->descriptorSet ?
		(ID<DescriptorSet>)spriteRenderComponent->descriptorSet : defaultDescriptorSet);
}

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

uint64 SpriteRenderSystem::getInstanceDataSize()
{
	return sizeof(InstanceData);
}

//**********************************************************************************************************************
void SpriteRenderSystem::tryDestroyResources(View<SpriteRenderComponent> spriteComponent)
{
	GARDEN_ASSERT(spriteComponent);
	auto resourceSystem = ResourceSystem::getInstance();
	resourceSystem->destroyShared(spriteComponent->colorMap);
	resourceSystem->destroyShared(spriteComponent->descriptorSet);
	spriteComponent->colorMap = {};
	spriteComponent->descriptorSet = {};
}
void SpriteRenderSystem::copyComponent(View<SpriteRenderComponent> sourceComponent,
	View<SpriteRenderComponent> destinationComponent)
{
	destinationComponent->aabb = sourceComponent->aabb;
	destinationComponent->isEnabled = sourceComponent->isEnabled;
	destinationComponent->isArray = sourceComponent->isArray;
	destinationComponent->colorMap = sourceComponent->colorMap;
	destinationComponent->descriptorSet = sourceComponent->descriptorSet;
	destinationComponent->colorMapLayer = sourceComponent->colorMapLayer;
	destinationComponent->colorFactor = sourceComponent->colorFactor;
	destinationComponent->uvSize = sourceComponent->uvSize;
	destinationComponent->uvOffset = sourceComponent->uvOffset;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationComponent->path = sourceComponent->path;
	#endif
}

//**********************************************************************************************************************
void SpriteRenderSystem::serialize(ISerializer& serializer, 
	ID<Entity> entity, View<SpriteRenderComponent> component)
{
	if (component->isArray != false)
		serializer.write("isArray", component->isArray);
	if (component->aabb != Aabb::one)
		serializer.write("aabb", component->aabb);
	if (component->isEnabled != true)
		serializer.write("isEnabled", component->isEnabled);
	if (component->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", component->colorMapLayer);
	if (component->colorFactor != float4(1.0f))
		serializer.write("colorFactor", component->colorFactor);
	if (component->uvSize != float2(1.0f))
		serializer.write("uvSize", component->uvSize);
	if (component->uvOffset != float2(0.0f))
		serializer.write("uvOffset", component->uvOffset);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("path", component->path);
	#endif
}
void SpriteRenderSystem::deserialize(IDeserializer& deserializer, 
	ID<Entity> entity, View<SpriteRenderComponent> component)
{
	deserializer.read("isArray", component->isArray);
	deserializer.read("aabb", component->aabb);
	deserializer.read("isEnabled", component->isEnabled);
	deserializer.read("colorMapLayer", component->colorMapLayer);
	deserializer.read("colorFactor", component->colorFactor);
	deserializer.read("uvSize", component->uvSize);
	deserializer.read("uvOffset", component->uvOffset);
	deserializer.read("path", component->path);

	if (component->path.empty())
		component->path = "missing";
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (component->isArray)
		flags |= ImageLoadFlags::LoadArray;
	component->colorMap = ResourceSystem::getInstance()->loadImage(component->path,
		Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
}

//**********************************************************************************************************************
void SpriteRenderSystem::serializeAnimation(ISerializer& serializer, View<SpriteRenderFrame> frame)
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
void SpriteRenderSystem::deserializeAnimation(IDeserializer& deserializer, SpriteRenderFrame& frame)
{
	frame.animateIsEnabled = deserializer.read("isEnabled", frame.isEnabled);
	frame.animateColorFactor = deserializer.read("colorFactor", frame.colorFactor);
	frame.animateUvSize = deserializer.read("uvSize", frame.uvSize);
	frame.animateUvOffset = deserializer.read("uvOffset", frame.uvOffset);
	frame.animateColorMapLayer = deserializer.read("colorMapLayer", frame.colorMapLayer);
}