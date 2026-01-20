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

#include "garden/editor/system/render/fxaa.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/fxaa.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
FxaaRenderEditorSystem::FxaaRenderEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", FxaaRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FxaaRenderEditorSystem::deinit);
}
FxaaRenderEditorSystem::~FxaaRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FxaaRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FxaaRenderEditorSystem::deinit);
	}
}

void FxaaRenderEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", FxaaRenderEditorSystem::editorSettings);
}
void FxaaRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", FxaaRenderEditorSystem::editorSettings);
	}
}

void FxaaRenderEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("FXAA (Anti-aliasing)"))
	{
		ImGui::Indent();
		ImGui::PushID("fxaa");

		auto fxaaSystem = FxaaRenderSystem::Instance::get();
		if (ImGui::Checkbox("Enabled", &fxaaSystem->isEnabled))
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setBool("fxaa.enabled", fxaaSystem->isEnabled);
		}

		auto quality = fxaaSystem->getQuality();
		auto subpixelQualit = fxaaSystem->getSubpixelQuality();

		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			fxaaSystem->setQuality(quality, subpixelQualit);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("fxaa.quality", toString((GraphicsQuality)quality));
		}
		if (ImGui::SliderFloat("Subpixel Quality", &subpixelQualit, 0.0f, 1.0f))
		{
			fxaaSystem->setQuality(quality, subpixelQualit);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setFloat("fxaa.subpixelQuality", subpixelQualit);
		}
		ImGui::Checkbox("Visualize", &fxaaSystem->visualize);

		ImGui::PopID();
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif