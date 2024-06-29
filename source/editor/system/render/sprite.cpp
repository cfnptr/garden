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

#include "garden/editor/system/render/sprite.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#include "garden/system/render/sprite/cutout.hpp"

using namespace garden;

SpriteRenderEditorSystem::SpriteRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", SpriteRenderEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
}
SpriteRenderEditorSystem::~SpriteRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", SpriteRenderEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
	}
}

void SpriteRenderEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	auto editorSystem = EditorRenderSystem::getInstance();
	if (manager->has<CutoutSpriteSystem>())
	{
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
}
void SpriteRenderEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->tryUnregisterEntityInspector<CutoutSpriteComponent>();
}

//**********************************************************************************************************************
static void renderSpriteComponent(SpriteRenderComponent* spriteComponent, type_index componentType)
{
	auto manager = Manager::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	auto flags = ImageLoadFlags::ArrayType;
	if (spriteComponent->isArray)
		flags |= ImageLoadFlags::LoadArray;
	editorSystem->drawImageSelector(spriteComponent->path, spriteComponent->colorMap,
		spriteComponent->descriptorSet, spriteComponent->getEntity(), componentType, flags);
	editorSystem->drawResource(spriteComponent->descriptorSet);

	ImGui::Checkbox("Enabled", &spriteComponent->isEnabled);

	if (ImGui::Checkbox("Array", &spriteComponent->isArray) && !spriteComponent->path.empty())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		if (spriteComponent->colorMap.getRefCount() == 1)
			graphicsSystem->destroy(spriteComponent->colorMap);
		if (spriteComponent->descriptorSet.getRefCount() == 1)
			graphicsSystem->destroy(spriteComponent->descriptorSet);

		auto flags = ImageLoadFlags::ArrayType;
		if (spriteComponent->isArray)
			flags |= ImageLoadFlags::LoadArray;
		spriteComponent->colorMap = ResourceSystem::getInstance()->loadImage(spriteComponent->path, 
			Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
		spriteComponent->descriptorSet = {};
	}

	auto& aabb = spriteComponent->aabb;
	ImGui::DragFloat3("Min AABB", (float3*)&aabb.getMin(), 0.01f);
	ImGui::DragFloat3("Max AABB", (float3*)&aabb.getMax(), 0.01f);
	ImGui::DragFloat2("UV Size", &spriteComponent->uvSize, 0.01f);
	ImGui::DragFloat2("UV Offset", &spriteComponent->uvOffset, 0.01f);
	ImGui::SliderFloat4("Color Factor", &spriteComponent->colorFactor, 0.0f, 1.0f);
}

void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (isOpened)
	{
		auto cutoutComponent = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		renderSpriteComponent(*cutoutComponent, typeid(CutoutSpriteComponent));
		ImGui::SliderFloat("Alpha Cutoff", &cutoutComponent->alphaCutoff, 0.0f, 1.0f);
	}
}
#endif