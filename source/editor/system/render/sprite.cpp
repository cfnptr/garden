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
#include "garden/system/render/sprite/opaque.hpp"
#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/sprite/translucent.hpp"

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
		editorSystem->registerEntityInspector<OpaqueSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
		editorSystem->registerEntityInspector<TranslucentSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTranslucentEntityInspector(entity, isOpened);
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
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (spriteComponent->isArray)
		flags |= ImageLoadFlags::LoadArray;
	editorSystem->drawImageSelector(spriteComponent->path, spriteComponent->colorMap,
		spriteComponent->descriptorSet, spriteComponent->getEntity(), componentType, flags);
	editorSystem->drawResource(spriteComponent->descriptorSet);

	ImGui::Checkbox("Enabled", &spriteComponent->isEnabled); ImGui::SameLine();

	if (ImGui::Checkbox("Array", &spriteComponent->isArray) && !spriteComponent->path.empty())
	{
		auto resourceSystem = ResourceSystem::getInstance();
		resourceSystem->destroyShared(spriteComponent->colorMap);
		resourceSystem->destroyShared(spriteComponent->descriptorSet);

		auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
		if (spriteComponent->isArray)
			flags |= ImageLoadFlags::LoadArray;
		spriteComponent->colorMap = ResourceSystem::getInstance()->loadImage(spriteComponent->path,
			Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
		spriteComponent->descriptorSet = {};
	}

	ImGui::BeginDisabled(!spriteComponent->colorMap && manager->has<TransformComponent>(spriteComponent->getEntity()));
	if (ImGui::Button("Auto Scale", ImVec2(-FLT_MIN, 0.0f)))
	{
		auto transformComponent = manager->get<TransformComponent>(spriteComponent->getEntity());
		auto colorMapView = GraphicsSystem::getInstance()->get(spriteComponent->colorMap);
		auto imageSize = colorMapView->getSize();

		if (imageSize.x > imageSize.y)
		{
			transformComponent->scale.x = spriteComponent->uvSize.x *
				transformComponent->scale.y * ((float)imageSize.x / imageSize.y);
		}
		else
		{
			transformComponent->scale.y = spriteComponent->uvSize.y * 
				transformComponent->scale.x * ((float)imageSize.y / imageSize.x);
		}
	}
	ImGui::EndDisabled();

	auto maxColorMapLayer = 0.0f;
	if (spriteComponent->colorMap)
	{
		auto colorMapView = GraphicsSystem::getInstance()->get(spriteComponent->colorMap);
		maxColorMapLayer = colorMapView->getLayerCount() - 1;
	}

	auto& aabb = spriteComponent->aabb;
	ImGui::DragFloat3("Min AABB", (float3*)&aabb.getMin(), 0.01f);
	ImGui::DragFloat3("Max AABB", (float3*)&aabb.getMax(), 0.01f);
	ImGui::DragFloat2("UV Size", &spriteComponent->uvSize, 0.01f);
	ImGui::DragFloat2("UV Offset", &spriteComponent->uvOffset, 0.01f);
	ImGui::SliderFloat("Color Layer", &spriteComponent->colorMapLayer, 0.0f, maxColorMapLayer);
	ImGui::SliderFloat4("Color Factor", &spriteComponent->colorFactor, 0.0f, 1.0f);
}

void SpriteRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<OpaqueSpriteComponent>(entity);
		renderSpriteComponent(*componentView, typeid(OpaqueSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		renderSpriteComponent(*componentView, typeid(CutoutSpriteComponent));
		ImGui::SliderFloat("Alpha Cutoff", &componentView->alphaCutoff, 0.0f, 1.0f);
	}
}
void SpriteRenderEditorSystem::onTranslucentEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<TranslucentSpriteComponent>(entity);
		renderSpriteComponent(*componentView, typeid(TranslucentSpriteComponent));
	}
}
#endif