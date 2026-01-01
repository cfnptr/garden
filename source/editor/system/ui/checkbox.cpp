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

#include "garden/editor/system/ui/checkbox.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/checkbox.hpp"

using namespace garden;

//**********************************************************************************************************************
UiCheckboxEditorSystem::UiCheckboxEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiCheckboxEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiCheckboxEditorSystem::deinit);
}
UiCheckboxEditorSystem::~UiCheckboxEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiCheckboxEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiCheckboxEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiCheckboxEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiCheckboxComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiCheckboxEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiCheckboxComponent>();
}

//**********************************************************************************************************************
void UiCheckboxEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiCheckboxView = Manager::Instance::get()->get<UiCheckboxComponent>(entity);

	auto isEnabled = uiCheckboxView->isEnabled();
	if (ImGui::Checkbox("Enabled", &isEnabled))
		uiCheckboxView->setEnabled(isEnabled);
	ImGui::SameLine();

	auto isChecked = uiCheckboxView->isChecked();
	if (ImGui::Checkbox("Checked", &isChecked))
		uiCheckboxView->setChecked(isChecked);

	ImGui::InputText("On Change", &uiCheckboxView->onChange);
	if (ImGui::BeginPopupContextItem("onChange"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiCheckboxView->onChange = "";
		ImGui::EndPopup();
	}

	ImGui::InputText("Animation Path", &uiCheckboxView->animationPath);
	if (ImGui::BeginPopupContextItem("animationPath"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiCheckboxView->animationPath = "";
		ImGui::EndPopup();
	}
}
#endif