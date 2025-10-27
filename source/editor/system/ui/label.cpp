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

	if (ImGui::InputText("Value", &uiLabelView->value))
		uiLabelView->updateText(true);
	ImGui::Checkbox("Load Noto", &uiLabelView->loadNoto);
	ImGui::Spacing();
	
	if (ImGui::CollapsingHeader("Fonts"))
	{
		auto& fontPaths = uiLabelView->fontPaths;
		for (auto i = fontPaths.begin(); i != fontPaths.end(); i++)
		{
			auto index = to_string(fontPaths.end() - i);
			auto pathString = i->generic_string();

			ImGui::PushID(index.c_str());
			if (ImGui::InputText(index.c_str(), &pathString))
				*i = pathString;
			ImGui::SameLine();

			if (ImGui::SmallButton(" - "))
				i = fontPaths.erase(i);
			ImGui::PopID();
		}

		ImGui::BeginDisabled(fontPaths.empty());
		if (ImGui::SmallButton(" - ") && fontPaths.size() > 0)
			fontPaths.resize(fontPaths.size() - 1);
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::SmallButton(" + "))
			fontPaths.push_back("");
		ImGui::Spacing();
	}
}
#endif