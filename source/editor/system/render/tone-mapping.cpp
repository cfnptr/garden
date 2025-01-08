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

#include "garden/editor/system/render/tone-mapping.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/pbr-lighting.hpp"

using namespace garden;

//**********************************************************************************************************************
ToneMappingRenderEditorSystem::ToneMappingRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", ToneMappingRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ToneMappingRenderEditorSystem::deinit);
}
ToneMappingRenderEditorSystem::~ToneMappingRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ToneMappingRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ToneMappingRenderEditorSystem::deinit);
	}
}

void ToneMappingRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", ToneMappingRenderEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", ToneMappingRenderEditorSystem::editorBarTool);
}
void ToneMappingRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", ToneMappingRenderEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", ToneMappingRenderEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void ToneMappingRenderEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::Instance::get()->canRender())
		return;

	if (ImGui::Begin("Tone Mapping", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto toneMappingSystem = ToneMappingRenderSystem::Instance::get();

		constexpr auto toneMapperTypes = "ACES\0Uchimura\0\0";
		if (ImGui::Combo("Tone Mapper", &toneMapper, toneMapperTypes))
			toneMappingSystem->setConsts(toneMappingSystem->getUseBloomBuffer(), toneMapper);

		ImGui::DragFloat("Exposure Coefficient", &toneMappingSystem->exposureFactor, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Dither Intensity", &toneMappingSystem->ditherIntensity, 0.0f, 1.0f);

		auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
		ImGui::ColorEdit3("Shadow Color", &pbrLightingSystem->shadowColor,
			ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

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

void ToneMappingRenderEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Tone Mapping"))
		showWindow = true;
}
#endif