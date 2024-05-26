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
	InstanceRenderSystem::deinit();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SpriteRenderSystem::imageLoaded);
}

//**********************************************************************************************************************
void SpriteRenderSystem::imageLoaded()
{
	auto image = ResourceSystem::getInstance()->getLoadedImage();
	auto& componentPool = getMeshComponentPool();
	auto componentSize = getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto componentOccupnacy = componentPool.getOccupancy();
	Ref<DescriptorSet> sharedDescriptorSet = {};

	for (uint32 i = 0; i < componentOccupnacy; i++)
	{
		auto spriteRender = (SpriteRenderComponent*)(componentData + i * componentSize);
		if (spriteRender->colorMap != image)
			continue;

		if (sharedDescriptorSet)
		{
			spriteRender->descriptorSet = sharedDescriptorSet;
			continue;
		}
		
		auto graphicsSystem = GraphicsSystem::getInstance();
		auto imageView = graphicsSystem->get(image);
		auto uniforms = getSpriteUniforms(imageView->getDefaultView());
		sharedDescriptorSet = graphicsSystem->createDescriptorSet(getPipeline(), std::move(uniforms), 1);
		spriteRender->descriptorSet = sharedDescriptorSet;
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, sharedDescriptorSet,
			"descriptorSet.sprite." + to_string(i + 1));
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
	auto instanceData = (InstanceData*)instanceMap;
	auto& instance = instanceData[drawIndex];
	instance.mvp = viewProj * model;

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
	{ { "colorMap", DescriptorSet::Uniform(colorMap) }, };
	return spriteUniforms;
}
map<string, DescriptorSet::Uniform> SpriteRenderSystem::getDefaultUniforms()
{
	map<string, DescriptorSet::Uniform> defaultUniforms =
	{ { "colorMap", DescriptorSet::Uniform(GraphicsSystem::getInstance()->getWhiteTexture()) }, };
	return defaultUniforms;
}

void SpriteRenderSystem::destroyResources(SpriteRenderComponent* spriteComponent)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (spriteComponent->colorMap.getRefCount() == 1)
		graphicsSystem->destroy(spriteComponent->colorMap);
	if (spriteComponent->descriptorSet.getRefCount() == 1)
		graphicsSystem->destroy(spriteComponent->descriptorSet);
}
uint64 SpriteRenderSystem::getInstanceDataSize()
{
	return sizeof(InstanceData);
}