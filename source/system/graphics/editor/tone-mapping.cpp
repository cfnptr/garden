//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/graphics/editor/tone-mapping.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"
#include "garden/system/graphics/lighting.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
ToneMappingEditor::ToneMappingEditor(ToneMappingRenderSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
static float exposureValue = 1.0f;

void ToneMappingEditor::render()
{
	if (!showWindow) return;

	if (ImGui::Begin("Tone Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const auto toneMapperTypes = "ACES\0Uchimura\0\0";
		if (ImGui::Combo("Tone Mapper", toneMapperType, toneMapperTypes))
			system->setConsts(system->useBloomBuffer, (ToneMapper)toneMapperType);

		ImGui::DragFloat("Exposure Coefficient",
			&system->exposureCoeff, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Dither Intensity", &system->ditherIntensity, 0.0f, 1.0f);

		auto lightingSystem = system->getManager()->get<LightingRenderSystem>();
		ImGui::ColorEdit4("Shadow Color", (float*)&lightingSystem->shadowColor, 
			ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

		if (ImGui::CollapsingHeader("Set Exposure / Luminance"))
		{
			ImGui::DragFloat("Value", &exposureValue, 0.01f);
			if (ImGui::Button("Set Exposure")) system->setExposure(exposureValue);
			ImGui::SameLine();
			if (ImGui::Button("Set Luminance")) system->setLuminance(exposureValue);
		}
	}
	ImGui::End();
}

void ToneMappingEditor::onBarTool()
{
	if (ImGui::MenuItem("Tone Mapping")) showWindow = true;
}
#endif