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

#include "garden/editor/system/render/hbao.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/hbao.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
HbaoRenderEditorSystem::HbaoRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", HbaoRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", HbaoRenderEditorSystem::deinit);
}
HbaoRenderEditorSystem::~HbaoRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", HbaoRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", HbaoRenderEditorSystem::deinit);
	}
}

void HbaoRenderEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", HbaoRenderEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", HbaoRenderEditorSystem::editorBarToolPP);
}
void HbaoRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", HbaoRenderEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", HbaoRenderEditorSystem::editorBarToolPP);
	}
}

void HbaoRenderEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("HBAO (Ambient Occlusion)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto hbaoSystem = HbaoRenderSystem::Instance::get();
		if (ImGui::Checkbox("Enabled", &hbaoSystem->isEnabled))
		{
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setBool("hbao.enabled", hbaoSystem->isEnabled);
		}

		ImGui::DragFloat("Radius", &hbaoSystem->radius, 0.01f, 0.0f, FLT_MAX);
		ImGui::SliderFloat("Bias", &hbaoSystem->bias, 0.0f, 0.999f);
		ImGui::SliderFloat("Intensity", &hbaoSystem->intensity, 0.0f, 4.0f);

		int stepCount = hbaoSystem->getStepCount();
		if (ImGui::InputInt("Step Count", &stepCount))
			hbaoSystem->setConsts(std::max(stepCount, 1));
	}
	ImGui::End();
}

void HbaoRenderEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("HBAO (Ambient Occlusion)"))
		showWindow = true;
}
#endif