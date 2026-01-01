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

#include "garden/editor/system/ui/input.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/input.hpp"
#include "garden/utf.hpp"

using namespace garden;

//**********************************************************************************************************************
UiInputEditorSystem::UiInputEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiInputEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiInputEditorSystem::deinit);
}
UiInputEditorSystem::~UiInputEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
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

	string text; UTF::convert(uiInputView->text, text);
	if (ImGui::InputText("Text", &text))
	{
		UTF::convert(text, uiInputView->text);
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

	UTF::convert(uiInputView->placeholder, text);
	if (ImGui::InputText("Placeholder", &text))
	{
		UTF::convert(text, uiInputView->placeholder);
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

	UTF::convert(uiInputView->prefix, text);
	if (ImGui::InputText("Prefix", &text))
	{
		UTF::convert(text, uiInputView->prefix);
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

	auto maxLength = uiInputView->maxLength == UINT32_MAX ? 
		0 : (int)uiInputView->maxLength;
	if (ImGui::DragInt("Max Length", &maxLength))
	{
		uiInputView->maxLength = maxLength <= 0 ? UINT32_MAX : maxLength;
		uiInputView->updateText();
	}
	if (ImGui::BeginPopupContextItem("maxLength"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiInputView->maxLength = UINT32_MAX;
			uiInputView->updateText();
		}
		ImGui::EndPopup();
	}

	u32string_view replaceChar((char32_t*)&uiInputView->replaceChar, 1);
	UTF::convert(replaceChar, text);
	if (ImGui::InputText("Replace Char", &text))
	{
		u32string utf32; UTF::convert(text, utf32);
		uiInputView->replaceChar = utf32.empty() ? 0 : (uint32)utf32[0];
		uiInputView->updateText();
	}
	if (ImGui::BeginPopupContextItem("replaceChar"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			uiInputView->replaceChar = 0;
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