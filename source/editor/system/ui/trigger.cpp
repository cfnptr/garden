// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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

#include "garden/editor/system/ui/trigger.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/trigger.hpp"

using namespace garden;

//**********************************************************************************************************************
UiTriggerEditorSystem::UiTriggerEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiTriggerEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiTriggerEditorSystem::deinit);
}
UiTriggerEditorSystem::~UiTriggerEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiTriggerEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiTriggerEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiTriggerEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiTriggerComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiTriggerEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiTriggerComponent>();
}

//**********************************************************************************************************************
void UiTriggerEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiTriggerView = Manager::Instance::get()->get<UiTriggerComponent>(entity);

	ImGui::DragFloat2("Offset", &uiTriggerView->offset, 1.0f);
	if (ImGui::BeginPopupContextItem("offset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiTriggerView->offset = float2::zero;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("Scale", &uiTriggerView->scale, 1.0f, 0.0001f, FLT_MAX);
	if (ImGui::BeginPopupContextItem("scale"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiTriggerView->scale = float2::one;
		ImGui::EndPopup();
	}

	ImGui::InputText("On Enter", &uiTriggerView->onEnter);
	ImGui::InputText("On Exit", &uiTriggerView->onExit);
	ImGui::InputText("On Stay", &uiTriggerView->onStay);
}
#endif