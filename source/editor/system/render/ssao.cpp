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

#include "garden/editor/system/render/ssao.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/ssao.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
SsaoRenderEditorSystem::SsaoRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SsaoRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SsaoRenderEditorSystem::deinit);
}
SsaoRenderEditorSystem::~SsaoRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SsaoRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SsaoRenderEditorSystem::deinit);
	}
}

void SsaoRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", SsaoRenderEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", SsaoRenderEditorSystem::editorBarToolPP);
}
void SsaoRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", SsaoRenderEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", SsaoRenderEditorSystem::editorBarToolPP);
	}
}

void SsaoRenderEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("SSAO (Ambient Occlusion)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto ssaoSystem = SsaoRenderSystem::Instance::get();
		if (ImGui::Checkbox("Enabled", &ssaoSystem->isEnabled))
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setBool("ssao.isEnabled", ssaoSystem->isEnabled);
		}

		ImGui::DragFloat("Radius", &ssaoSystem->radius, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Bias", &ssaoSystem->bias, 0.0f, 1.0f);
		ImGui::SliderFloat("Intensity", &ssaoSystem->intensity, 0.0f, 1.0f);

		int sampleCount = ssaoSystem->getSampleCount();
		if (ImGui::InputInt("Sample Count", &sampleCount))
			ssaoSystem->setConsts(std::abs(sampleCount));
	}
	ImGui::End();
}

void SsaoRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("SSAO (Ambient Occlusion)"))
		showWindow = true;
}
#endif