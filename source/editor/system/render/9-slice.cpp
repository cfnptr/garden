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

#include "garden/editor/system/render/9-slice.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#include "garden/system/render/9-slice/opaque.hpp"
// #include "garden/system/render/9-slice/cutout.hpp"
// #include "garden/system/render/9-slice/translucent.hpp"
#include "garden/editor/system/render/sprite.hpp"

using namespace garden;

//**********************************************************************************************************************
NineSliceRenderEditorSystem::NineSliceRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", NineSliceRenderEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", NineSliceRenderEditorSystem::deinit);
}
NineSliceRenderEditorSystem::~NineSliceRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", NineSliceRenderEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", NineSliceRenderEditorSystem::deinit);
	}
}

void NineSliceRenderEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	auto editorSystem = EditorRenderSystem::getInstance();
	if (manager->has<Opaque9SliceSystem>())
	{
		editorSystem->registerEntityInspector<Opaque9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	/*if (manager->has<Cutout9SliceSystem>())
	{
		editorSystem->registerEntityInspector<Cutout9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (manager->has<Translucent9SliceSystem>())
	{
		editorSystem->registerEntityInspector<Translucent9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTranslucentEntityInspector(entity, isOpened);
		});
	}*/
}
void NineSliceRenderEditorSystem::deinit()
{
	auto editorSystem = EditorRenderSystem::getInstance();
	editorSystem->tryUnregisterEntityInspector<Opaque9SliceComponent>();
	/*editorSystem->tryUnregisterEntityInspector<Cutout9SliceComponent>();
	editorSystem->tryUnregisterEntityInspector<Translucent9SliceComponent>();*/
}

//**********************************************************************************************************************
void NineSliceRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<Opaque9SliceComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : 
			componentView->path.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<Opaque9SliceComponent>(entity);
		renderComponent(*componentView, typeid(Opaque9SliceComponent));
	}
}
void NineSliceRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	/*if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<Cutout9SliceComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : componentView->path.c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<Cutout9SliceComponent>(entity);
		renderSpriteComponent(*componentView, typeid(Cutout9SliceComponent));

		ImGui::SliderFloat("Alpha Cutoff", &componentView->alphaCutoff, 0.0f, 1.0f);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				componentView->alphaCutoff = 0.5f;
			ImGui::EndPopup();
		}
	}*/
}
void NineSliceRenderEditorSystem::onTranslucentEntityInspector(ID<Entity> entity, bool isOpened)
{
	/*if (ImGui::BeginItemTooltip())
	{
		auto componentView = Manager::getInstance()->get<Translucent9SliceComponent>(entity);
		ImGui::Text("Path: %s", componentView->path.empty() ? "<null>" : componentView->path.c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto componentView = Manager::getInstance()->get<Translucent9SliceComponent>(entity);
		renderSpriteComponent(*componentView, typeid(Translucent9SliceComponent));
	}*/
}

//**********************************************************************************************************************
void NineSliceRenderEditorSystem::renderComponent(NineSliceRenderComponent* componentView, type_index componentType)
{
	SpriteRenderEditorSystem::renderComponent(componentView, componentType);
	ImGui::DragFloat2("Texture Border", &componentView->textureBorder, 0.1f);
	ImGui::DragFloat2("Window Border", &componentView->windowBorder, 0.1f);
}
#endif