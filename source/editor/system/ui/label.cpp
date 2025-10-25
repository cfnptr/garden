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

#include "garden/editor/system/ui/label.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/label.hpp"

using namespace garden;

//**********************************************************************************************************************
UiLabelEditorSystem::UiLabelEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiLabelEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiLabelEditorSystem::deinit);
}
UiLabelEditorSystem::~UiLabelEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiLabelEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiLabelEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiLabelEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiLabelComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiLabelEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiLabelComponent>();
}

//**********************************************************************************************************************
void UiLabelEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiLabelView = UiLabelSystem::Instance::get()->getComponent(entity);

	auto value = uiLabelView->getValue();
	if (ImGui::InputText("Value", &value))
		uiLabelView->setValue(value);
}
#endif