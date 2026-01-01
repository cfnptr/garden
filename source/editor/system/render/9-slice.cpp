// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/render/9-slice/ui.hpp"
#include "garden/system/render/9-slice/opaque.hpp"
#include "garden/system/render/9-slice/cutout.hpp"
#include "garden/system/render/9-slice/translucent.hpp"
#include "garden/editor/system/render/sprite.hpp"

using namespace garden;

NineSliceEditorSystem::NineSliceEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", NineSliceEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", NineSliceEditorSystem::deinit);
}
NineSliceEditorSystem::~NineSliceEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", NineSliceEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", NineSliceEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void NineSliceEditorSystem::init()
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	if (Opaque9SliceSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<Opaque9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	if (Cutout9SliceSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<Cutout9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (Trans9SliceSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<Trans9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTransEntityInspector(entity, isOpened);
		});
	}
	if (Ui9SliceSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<Ui9SliceComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onUiEntityInspector(entity, isOpened);
		});
	}
}
void NineSliceEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto editorSystem = EditorRenderSystem::Instance::get();
		editorSystem->tryUnregisterEntityInspector<Opaque9SliceComponent>();
		editorSystem->tryUnregisterEntityInspector<Cutout9SliceComponent>();
		editorSystem->tryUnregisterEntityInspector<Trans9SliceComponent>();
		editorSystem->tryUnregisterEntityInspector<Ui9SliceComponent>();
	}
}

//**********************************************************************************************************************
template<class C>
static void renderNineSliceTooltip(ID<Entity> entity)
{
	if (ImGui::BeginItemTooltip())
	{
		auto nineSliceView = Manager::Instance::get()->get<C>(entity);
		ImGui::Text("Enabled: %s, Path: %s", nineSliceView->isEnabled ? "true" : "false",
			nineSliceView->colorMapPath.empty() ? "<null>" : nineSliceView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
}
void NineSliceEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderNineSliceTooltip<Opaque9SliceComponent>(entity);
	if (isOpened)
	{
		auto opaque9SliceView = Manager::Instance::get()->get<Opaque9SliceComponent>(entity);
		renderComponent(*opaque9SliceView, typeid(Opaque9SliceComponent));
	}
}
void NineSliceEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderNineSliceTooltip<Cutout9SliceComponent>(entity);
	if (isOpened)
	{
		auto cutout9SliceView = Manager::Instance::get()->get<Cutout9SliceComponent>(entity);
		renderComponent(*cutout9SliceView, typeid(Cutout9SliceComponent));

		ImGui::SliderFloat("Alpha Cutoff", &cutout9SliceView->alphaCutoff, 0.0f, 1.0f);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cutout9SliceView->alphaCutoff = 0.5f;
			ImGui::EndPopup();
		}
	}
}
void NineSliceEditorSystem::onTransEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderNineSliceTooltip<Trans9SliceComponent>(entity);
	if (isOpened)
	{
		auto trans9SliceView = Manager::Instance::get()->get<Trans9SliceComponent>(entity);
		renderComponent(*trans9SliceView, typeid(Trans9SliceComponent));
	}
}
void NineSliceEditorSystem::onUiEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderNineSliceTooltip<Ui9SliceComponent>(entity);
	if (isOpened)
	{
		auto ui9SliceView = Manager::Instance::get()->get<Ui9SliceComponent>(entity);
		renderComponent(*ui9SliceView, typeid(Ui9SliceComponent));
	}
}

//**********************************************************************************************************************
void NineSliceEditorSystem::renderComponent(NineSliceComponent* componentView, type_index componentType)
{
	GARDEN_ASSERT(componentView);
	SpriteRenderEditorSystem::renderComponent(componentView, componentType);

	ImGui::DragFloat2("Texture Border", &componentView->textureBorder, 0.1f);
	if (ImGui::BeginPopupContextItem("textureBorder"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->textureBorder = float2::zero;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("Window Border", &componentView->windowBorder, 0.1f);
	if (ImGui::BeginPopupContextItem("windowBorder"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->windowBorder = float2::zero;
		ImGui::EndPopup();
	}
}
#endif