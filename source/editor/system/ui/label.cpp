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

	auto uiLabelView = Manager::Instance::get()->get<UiLabelComponent>(entity);

	string text; UTF::convert(uiLabelView->text, text);
	if (ImGui::InputTextMultiline("Text", &text))
	{
		UTF::convert(text, uiLabelView->text);
		uiLabelView->updateText();
	}

	ImGui::Checkbox("Enabled", &uiLabelView->isEnabled); ImGui::SameLine();
	if (ImGui::Checkbox("Use Locale", &uiLabelView->useLocale))
		uiLabelView->updateText();
	ImGui::SameLine();
	if (ImGui::Checkbox("Adjust KJC", &uiLabelView->adjustKJC))
		uiLabelView->updateText();

	if (ImGui::Checkbox("Load Noto", &uiLabelView->loadNoto))
		uiLabelView->updateText();
	ImGui::SameLine();

	ImGui::BeginDisabled();
	auto isVisible = uiLabelView->isVisible();
	ImGui::Checkbox("Is Visible", &isVisible);
	ImGui::EndDisabled();
	ImGui::Spacing();

	auto fontSize = (int)uiLabelView->fontSize;
	if (ImGui::DragInt("Font Size", &fontSize, 1, 1, INT32_MAX))
	{
		uiLabelView->fontSize = fontSize;
		uiLabelView->updateText();
	}
	if (ImGui::BeginPopupContextItem("fontSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiLabelView->fontSize = 16;
			uiLabelView->updateText();
		}
		ImGui::EndPopup();
	}

	if (ImGui::Combo("Alignment", uiLabelView->propterties.alignment, textAlignmentNames, (int)Text::Alignment::Count))
		uiLabelView->updateText();
	if (ImGui::Checkbox("Bold", &uiLabelView->propterties.isBold))
		uiLabelView->updateText();
	ImGui::SameLine();
	if (ImGui::Checkbox("Italic", &uiLabelView->propterties.isItalic))
		uiLabelView->updateText();
	ImGui::SameLine();
	if (ImGui::Checkbox("Use Tags", &uiLabelView->propterties.useTags))
		uiLabelView->updateText();
	ImGui::Spacing();

	ImGui::ColorEdit4("Color", &uiLabelView->color);
	if (ImGui::BeginPopupContextItem("color"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiLabelView->color = f32x4::one;
		ImGui::EndPopup();
	}
	
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