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
	auto instance = (InstanceData*)(instanceMap + drawIndex * getInstanceDataSize());
	instance->mvp = viewProj * model;

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

void SpriteRenderSystem::tryDestroyResources(View<SpriteRenderComponent> spriteComponent)
{
	GARDEN_ASSERT(spriteComponent);
	auto resourceSystem = ResourceSystem::getInstance();
	resourceSystem->destroyShared(spriteComponent->colorMap);
	resourceSystem->destroyShared(spriteComponent->descriptorSet);
	spriteComponent->colorMap = {};
	spriteComponent->descriptorSet = {};
}