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

#include "garden/editor/system/render/dlss.hpp"

#if GARDEN_EDITOR && GARDEN_NVIDIA_DLSS
#include "garden/system/render/dlss.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
DlssRenderEditorSystem::DlssRenderEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", DlssRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", DlssRenderEditorSystem::deinit);
}
DlssRenderEditorSystem::~DlssRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", DlssRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", DlssRenderEditorSystem::deinit);
	}
}

void DlssRenderEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", DlssRenderEditorSystem::editorSettings);
}
void DlssRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", DlssRenderEditorSystem::editorSettings);
	}
}

void DlssRenderEditorSystem::editorSettings()
{
	ImGui::Spacing();
	ImGui::PushID("dlss");
	auto dlssSystem = DlssRenderSystem::Instance::get();
	auto quality = (int)dlssSystem->getQuality();
	if (ImGui::Combo("DLSS Quality", &quality, dlssQualityNames, (int)DlssQuality::Count))
	{
		dlssSystem->setQuality((DlssQuality)quality);
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		if (settingsSystem)
			settingsSystem->setString("dlss.quality", toString((DlssQuality)quality));
	}
	ImGui::PopID();
}
#endif