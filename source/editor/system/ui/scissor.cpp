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

#include "garden/editor/system/ui/scissor.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/scissor.hpp"

using namespace garden;

//**********************************************************************************************************************
UiScissorEditorSystem::UiScissorEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiScissorEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiScissorEditorSystem::deinit);
}
UiScissorEditorSystem::~UiScissorEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiScissorEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiScissorEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiScissorEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiScissorComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiScissorEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiScissorComponent>();
}

//**********************************************************************************************************************
void UiScissorEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiLabelView = Manager::Instance::get()->get<UiScissorComponent>(entity);

	ImGui::DragFloat2("Offset", &uiLabelView->offset, 1.0f);
	if (ImGui::BeginPopupContextItem("offset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiLabelView->scale = float2::zero;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("Scale", &uiLabelView->scale, 1.0f, 0.0001f, FLT_MAX);
	if (ImGui::BeginPopupContextItem("scale"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiLabelView->scale = float2::one;
		ImGui::EndPopup();
	}

	ImGui::Checkbox("Use Itself", &uiLabelView->useItsels);
}
#endif