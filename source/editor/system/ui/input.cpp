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

#include "garden/editor/system/ui/input.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/input.hpp"
#include "garden/utf.hpp"

using namespace garden;

//**********************************************************************************************************************
UiInputEditorSystem::UiInputEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiInputEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiInputEditorSystem::deinit);
}
UiInputEditorSystem::~UiInputEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiInputEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiInputEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiInputEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiInputComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiInputEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiInputComponent>();
}

//**********************************************************************************************************************
void UiInputEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto uiInputView = Manager::Instance::get()->get<UiInputComponent>(entity);

	auto isEnabled = uiInputView->isEnabled();
	if (ImGui::Checkbox("Enabled", &isEnabled))
		uiInputView->setEnabled(isEnabled);
	ImGui::SameLine();

	auto isTextBad = uiInputView->isTextBad();
	if (ImGui::Checkbox("Text Bad", &isTextBad))
		uiInputView->setTextBad(isTextBad);

	string text; UTF::utf32toUtf8(uiInputView->text, text);
	if (ImGui::InputText("Text", &text))
	{
		UTF::utf8toUtf32(text, uiInputView->text);
		uiInputView->updateText();
	}
	if (ImGui::BeginPopupContextItem("text"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiInputView->text = U"";
			uiInputView->updateText();
		}
		ImGui::EndPopup();
	}

	UTF::utf32toUtf8(uiInputView->placeholder, text);
	if (ImGui::InputText("Placeholder", &text))
	{
		UTF::utf8toUtf32(text, uiInputView->placeholder);
		uiInputView->updateText();
	}
	if (ImGui::BeginPopupContextItem("placeholder"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiInputView->placeholder = U"";
			uiInputView->updateText();
		}
		ImGui::EndPopup();
	}

	UTF::utf32toUtf8(uiInputView->prefix, text);
	if (ImGui::InputText("Prefix", &text))
	{
		UTF::utf8toUtf32(text, uiInputView->prefix);
		uiInputView->updateText();
	}
	if (ImGui::BeginPopupContextItem("prefix"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiInputView->prefix = U"";
			uiInputView->updateText();
		}
		ImGui::EndPopup();
	}

	ImGui::InputText("On Change", &uiInputView->onChange);
	if (ImGui::BeginPopupContextItem("onChange"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiInputView->onChange = "";
		ImGui::EndPopup();
	}

	ImGui::InputText("Animation Path", &uiInputView->animationPath);
	if (ImGui::BeginPopupContextItem("animationPath"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiInputView->animationPath = "";
		ImGui::EndPopup();
	}

	ImGui::ColorEdit4("Text Color", &uiInputView->textColor);
	if (ImGui::BeginPopupContextItem("textColor"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiInputView->textColor = f32x4::zero;
		ImGui::EndPopup();
	}

	ImGui::ColorEdit4("Placeholder Color", &uiInputView->placeholderColor);
	if (ImGui::BeginPopupContextItem("placeholderColor"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiInputView->placeholderColor = f32x4(0.5f, 0.5f, 0.5f, 1.0f);
		ImGui::EndPopup();
	}
}
#endif