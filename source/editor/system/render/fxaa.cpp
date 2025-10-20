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

#include "garden/editor/system/render/fxaa.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/fxaa.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
FxaaRenderEditorSystem::FxaaRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", FxaaRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FxaaRenderEditorSystem::deinit);
}
FxaaRenderEditorSystem::~FxaaRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FxaaRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FxaaRenderEditorSystem::deinit);
	}
}

void FxaaRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", FxaaRenderEditorSystem::editorSettings);
}
void FxaaRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", FxaaRenderEditorSystem::editorSettings);
}

void FxaaRenderEditorSystem::editorSettings()
{
	ImGui::Spacing();
	ImGui::PushID("fxaa");
	auto fxaaSystem = FxaaRenderSystem::Instance::get();
	if (ImGui::Checkbox("FXAA Enabled (Anti-aliasing)", &fxaaSystem->isEnabled))
	{
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		if (settingsSystem)
			settingsSystem->setBool("fxaa.enabled", fxaaSystem->isEnabled);
	}
	ImGui::PopID();
}
#endif