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

#include "garden/editor/system/render/tone-mapping.hpp"

#if GARDEN_EDITOR
using namespace garden;

//**********************************************************************************************************************
ToneMappingEditorSystem::ToneMappingEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", ToneMappingEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ToneMappingEditorSystem::deinit);
}
ToneMappingEditorSystem::~ToneMappingEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ToneMappingEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ToneMappingEditorSystem::deinit);
	}
}

void ToneMappingEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", ToneMappingEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", ToneMappingEditorSystem::editorBarToolPP);
}
void ToneMappingEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", ToneMappingEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", ToneMappingEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void ToneMappingEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Tone Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto toneMappingSystem = ToneMappingSystem::Instance::get();
		if (ImGui::Combo("Tone Mapper", toneMapper, TONE_MAPPER_NAMES, TONE_MAPPER_COUNT))
			toneMappingSystem->setConsts(toneMappingSystem->getUseBloomBuffer(), toneMapper);

		ImGui::DragFloat("Exposure Factor", &toneMappingSystem->exposureFactor, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Dither Intensity", &toneMappingSystem->ditherIntensity, 0.0f, 1.0f);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		const auto& commonConstants = graphicsSystem->getCommonConstants();
		auto emissiveCoeff = commonConstants.emissiveCoeff;
		if (ImGui::DragFloat("Emissive Coefficient", &emissiveCoeff, 0.1f, 0.0f, FLT_MAX))
			graphicsSystem->setEmissiveCoeff(emissiveCoeff);

		if (ImGui::CollapsingHeader("Set Exposure / Luminance"))
		{
			ImGui::DragFloat("Value", &exposureLuminance, 0.01f);
			if (ImGui::Button("Set Exposure"))
				toneMappingSystem->setExposure(exposureLuminance);
			ImGui::SameLine();
			if (ImGui::Button("Set Luminance"))
				toneMappingSystem->setLuminance(exposureLuminance);
		}
	}
	ImGui::End();
}

void ToneMappingEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Tone Mapping (HDR)"))
		showWindow = true;
}
#endif