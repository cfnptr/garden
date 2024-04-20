//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/editor/system/render/ssao.hpp"

#if GARDEN_EDITOR
#include "garden/system/settings.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
SsaoEditor::SsaoEditor(SsaoRenderSystem* system)
{
	EditorRenderSystem::getInstance()->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void SsaoEditor::render()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("SSAO (Ambient Occlusion)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::Checkbox("Enabled", &system->isEnabled))
		{
			auto settingsSystem = getManager()->tryGet<SettingsSystem>();
			if (settingsSystem)
				settingsSystem->setBool("useSSAO", system->isEnabled);
		}

		ImGui::DragFloat("Radius", &system->radius, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Bias", &system->bias, 0.0f, 1.0f);
		ImGui::SliderFloat("Intensity", &system->intensity, 0.0f, 1.0f);

		int sampleCount = system->sampleCount;
		if (ImGui::InputInt("Sample Count", &sampleCount))
			system->setConsts(std::abs(sampleCount));
	}
	ImGui::End();
}

void SsaoEditor::onBarTool()
{
	if (ImGui::MenuItem("SSAO (Ambient Occlusion)"))
		showWindow = true;
}
#endif