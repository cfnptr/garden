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

#include "garden/editor/system/render/clouds.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/clouds.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
CloudsEditorSystem::CloudsEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", CloudsEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", CloudsEditorSystem::deinit);
}
CloudsEditorSystem::~CloudsEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", CloudsEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", CloudsEditorSystem::deinit);
	}
}

void CloudsEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", CloudsEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", CloudsEditorSystem::editorBarToolPP);
}
void CloudsEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", CloudsEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", CloudsEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void CloudsEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Volumetric Clouds", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto cloudsSystem = CloudsRenderSystem::Instance::get();
		ImGui::Checkbox("Enabled", &cloudsSystem->isEnabled);

		auto quality = cloudsSystem->getQuality();
		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			cloudsSystem->setQuality(quality);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("clouds.quality", toString((GraphicsQuality)quality));
		}
		ImGui::Spacing();

		ImGui::DragFloat("Bottom Radius", &cloudsSystem->bottomRadius, 0.1f, 0.001f, FLT_MAX, "%.3f km");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Stratus and cumulus clouds start height above ground.");
			ImGui::EndTooltip();
		}
		ImGui::DragFloat("Top Radius", &cloudsSystem->topRadius, 0.1f, 0.001f, FLT_MAX, "%.3f km");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Stratus and cumulus clouds end height above ground.");
			ImGui::EndTooltip();
		}

		ImGui::DragFloat("Min Distance", &cloudsSystem->minDistance, 0.01f, 0.001f, FLT_MAX, "%.3f km");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Clouds volume tracing offset in front of camera.");
			ImGui::EndTooltip();
		}
		ImGui::DragFloat("Max Distance", &cloudsSystem->maxDistance, 0.01f, 0.001f, FLT_MAX, "%.3f km");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Maximum clouds volume tracing distance.");
			ImGui::EndTooltip();
		}

		ImGui::SliderFloat("Coverage", &cloudsSystem->coverage, 0.0f, 1.0f);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Ammount of clouds. (Clear or cloudy weather)");
			ImGui::EndTooltip();
		}
		ImGui::SliderFloat("Temperature", &cloudsSystem->temperature, 0.0f, 1.0f);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Temperature difference between layers. (Storm clouds)");
			ImGui::EndTooltip();
		}

		ImGui::DragFloat("Current Time", &cloudsSystem->currentTime, 0.1f, 0.0, 0.0f, "%.3f s");
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Custom current time value. (For a multiplayer sync)");
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void CloudsEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Volumetric Clouds"))
		showWindow = true;
}
#endif