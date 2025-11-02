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

#include "garden/editor/system/ui/button.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/button.hpp"

using namespace garden;

//**********************************************************************************************************************
UiButtonEditorSystem::UiButtonEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiButtonEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiButtonEditorSystem::deinit);
}
UiButtonEditorSystem::~UiButtonEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiButtonEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiButtonEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiButtonEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiButtonComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiButtonEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiButtonComponent>();
}

//**********************************************************************************************************************
void UiButtonEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiButtonView = Manager::Instance::get()->get<UiButtonComponent>(entity);

	auto isEnabled = uiButtonView->isEnabled();
	if (ImGui::Checkbox("Enabled", &isEnabled))
		uiButtonView->setEnabled(isEnabled);

	ImGui::InputText("On Click", &uiButtonView->onClick);
	ImGui::InputText("Animation Path", &uiButtonView->animationPath);
}
#endif