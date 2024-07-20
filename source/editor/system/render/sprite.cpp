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
	if (manager->has<OpaqueSpriteSystem>())
	{
		editorSystem->registerEntityInspector<OpaqueSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	if (manager->has<CutoutSpriteSystem>())
	{
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (manager->has<TranslucentSpriteSystem>())
	{
		editorSystem->registerEntityInspector<TranslucentSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTranslucentEntityInspector(entity, isOpened);
		});
	}
}
void SpriteRenderEditorSystem::deinit()
{
	auto editorSystem = EditorRenderSystem::getInstance();
	editorSystem->tryUnregisterEntityInspector<OpaqueSpriteComponent>();
	editorSystem->tryUnregisterEntityInspector<CutoutSpriteComponent>();
	editorSystem->tryUnregisterEntityInspector<TranslucentSpriteComponent>();
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<OpaqueSpriteComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : componentView->path.c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<OpaqueSpriteComponent>(entity);
		renderComponent(*componentView, typeid(OpaqueSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : componentView->path.c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		renderComponent(*componentView, typeid(CutoutSpriteComponent));

		ImGui::SliderFloat("Alpha Cutoff", &componentView->alphaCutoff, 0.0f, 1.0f);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				componentView->alphaCutoff = 0.5f;
			ImGui::EndPopup();
		}
	}
}
void SpriteRenderEditorSystem::onTranslucentEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<TranslucentSpriteComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : componentView->path.c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<TranslucentSpriteComponent>(entity);
		renderComponent(*componentView, typeid(TranslucentSpriteComponent));
	}
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::renderComponent(SpriteRenderComponent* component, type_index componentType)
{
	auto editorSystem = EditorRenderSystem::getInstance();
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (component->isArray)
		flags |= ImageLoadFlags::LoadArray;
	editorSystem->drawImageSelector(component->path, component->colorMap,
		component->descriptorSet, component->getEntity(), componentType, flags);
	editorSystem->drawResource(component->descriptorSet);

	ImGui::Checkbox("Enabled", &component->isEnabled); ImGui::SameLine();

	if (ImGui::Checkbox("Array", &component->isArray) && !component->path.empty())
	{
		auto resourceSystem = ResourceSystem::getInstance();
		resourceSystem->destroyShared(component->colorMap);
		resourceSystem->destroyShared(component->descriptorSet);

		auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
		if (component->isArray)
			flags |= ImageLoadFlags::LoadArray;
		component->colorMap = ResourceSystem::getInstance()->loadImage(component->path,
			Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
		component->descriptorSet = {};
	}

	auto transformSystem = TransformSystem::getInstance();
	ImGui::BeginDisabled(!component->colorMap && transformSystem->has(component->getEntity()));
	if (ImGui::Button("Auto Scale", ImVec2(-FLT_MIN, 0.0f)))
	{
		auto transformComponent = transformSystem->get(component->getEntity());
		auto colorMapView = GraphicsSystem::getInstance()->get(component->colorMap);
		auto imageSize = colorMapView->getSize();

		if (imageSize.x > imageSize.y)
		{
			transformComponent->scale.x = component->uvSize.x *
				transformComponent->scale.y * ((float)imageSize.x / imageSize.y);
		}
		else
		{
			transformComponent->scale.y = component->uvSize.y *
				transformComponent->scale.x * ((float)imageSize.y / imageSize.x);
		}
	}
	ImGui::EndDisabled();

	auto maxColorMapLayer = 0.0f;
	if (component->colorMap)
	{
		auto colorMapView = GraphicsSystem::getInstance()->get(component->colorMap);
		maxColorMapLayer = colorMapView->getLayerCount() - 1;
	}

	auto& aabb = component->aabb;
	ImGui::DragFloat3("Min AABB", (float3*)&aabb.getMin(), 0.01f);
	if (ImGui::BeginPopupContextItem("minAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			aabb = Aabb::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat3("Max AABB", (float3*)&aabb.getMax(), 0.01f);
	if (ImGui::BeginPopupContextItem("maxAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			aabb = Aabb::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Size", &component->uvSize, 0.01f);
	if (ImGui::BeginPopupContextItem("uvSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
			component->uvSize = float2(1.0f);
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Offset", &component->uvOffset, 0.01f);
	if (ImGui::BeginPopupContextItem("uvOffset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			component->uvOffset = float2(0.0f);
		ImGui::EndPopup();
	}

	ImGui::SliderFloat("Color Layer", &component->colorMapLayer, 0.0f, maxColorMapLayer);
	if (ImGui::BeginPopupContextItem("colorLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			component->colorMapLayer = 0.0f;
		ImGui::EndPopup();
	}

	ImGui::SliderFloat4("Color Factor", &component->colorFactor, 0.0f, 1.0f);
	if (ImGui::BeginPopupContextItem("colorFactor"))
	{
		if (ImGui::MenuItem("Reset Default"))
			component->colorFactor = float4(1.0f);
		ImGui::EndPopup();
	}
}
#endif