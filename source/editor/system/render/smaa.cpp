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

#include "garden/editor/system/render/smaa.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/smaa.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
SmaaRenderEditorSystem::SmaaRenderEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", SmaaRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SmaaRenderEditorSystem::deinit);
}
SmaaRenderEditorSystem::~SmaaRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SmaaRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SmaaRenderEditorSystem::deinit);
	}
}

void SmaaRenderEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", SmaaRenderEditorSystem::editorSettings);
}
void SmaaRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", SmaaRenderEditorSystem::editorSettings);
	}
}

void SmaaRenderEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("SMAA (Anti-aliasing)"))
	{
		ImGui::Indent();
		ImGui::PushID("smaa");

		auto smaaSystem = SmaaRenderSystem::Instance::get();
		if (ImGui::Checkbox("Enabled", &smaaSystem->isEnabled))
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setBool("smaa.enabled", smaaSystem->isEnabled);
		}

		auto quality = smaaSystem->getQuality();
		auto cornerRounding = (int)smaaSystem->getCornerRounding();

		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			smaaSystem->setQuality(quality, cornerRounding);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("smaa.quality", toString((GraphicsQuality)quality));
		}
		if (ImGui::SliderInt("Corner Rounding", &cornerRounding, 0, 100))
		{
			smaaSystem->setQuality(quality, cornerRounding);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setInt("smaa.cornerRounding", cornerRounding);
		}
		ImGui::Checkbox("Visualize", &smaaSystem->visualize);

		ImGui::PopID();
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif