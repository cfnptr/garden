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

#include "garden/editor/system/render/atmosphere.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/atmosphere.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
AtmosphereEditorSystem::AtmosphereEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", AtmosphereEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AtmosphereEditorSystem::deinit);
}
AtmosphereEditorSystem::~AtmosphereEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AtmosphereEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AtmosphereEditorSystem::deinit);
	}
}

void AtmosphereEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", AtmosphereEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", AtmosphereEditorSystem::editorBarToolPP);
}
void AtmosphereEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", AtmosphereEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", AtmosphereEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void AtmosphereEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Automatic Exposure (AE)", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
		ImGui::Checkbox("Enabled", &atmosphereSystem->isEnabled);

		auto quality = (int)atmosphereSystem->getQuality();
		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			atmosphereSystem->setQuality((GraphicsQuality)quality);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("atmosphere.quality", toString((GraphicsQuality)quality));
		}
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Planet"))
		{
			ImGui::DragFloat("Atmosphere Height", 
				(float*)&atmosphereSystem->atmosphereHeight, 0.1f, 0.001f, FLT_MAX, "%.3f km");
			ImGui::DragFloat("Ground Radius", 
				(float*)&atmosphereSystem->groundRadius, 10.0f, 0.001f, FLT_MAX, "%.3f km");
			ImGui::SliderFloat3("Ground Albedo", (float*)&atmosphereSystem->groundAlbedo, 0.0f, 1.0f);
			ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Sun"))
		{
			ImGui::PushID("sun");
			ImGui::DragFloat("Angular Size", (float*)&atmosphereSystem->sunAngularSize, 0.01f, 0.0f, 360.0f, "%.3f°");
			ImGui::ColorEdit3("Color", (float*)&atmosphereSystem->sunColor);
			ImGui::DragFloat("Luminance", (float*)&atmosphereSystem->sunColor.w, 100.0f, 0.0f, FLT_MAX);
			ImGui::PopID(); ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Rayleigh"))
		{
			ImGui::PushID("rayleigh");
			auto density = length(atmosphereSystem->rayleighScattering);
			auto scattering = atmosphereSystem->rayleighScattering / (density == 0.0f ? 1.0f : density);
			ImGui::ColorEdit3("Scattering", (float*)&scattering, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			ImGui::DragFloat("Density", (float*)&density, 0.001f, 0.0f, FLT_MAX, "%.4f");
			ImGui::DragFloat("Scale Height", 
				(float*)&atmosphereSystem->rayleightScaleHeight, 0.01f, 0.001f, FLT_MAX, "%.3f km");
			atmosphereSystem->rayleighScattering = scattering * max(density, 0.000001f);
			ImGui::PopID(); ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Mie"))
		{
			ImGui::PushID("mie");
			auto density = length(atmosphereSystem->mieScattering);
			auto scattering = atmosphereSystem->mieScattering / (density == 0.0f ? 1.0f : density);
			auto factor = length(atmosphereSystem->mieAbsorption);
			auto absorption = atmosphereSystem->mieAbsorption / (factor == 0.0f ? 1.0f : factor);
			ImGui::ColorEdit3("Scattering", (float*)&scattering, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			ImGui::DragFloat("Density", (float*)&density, 0.001f, 0.0f, FLT_MAX, "%.4f");
			ImGui::ColorEdit3("Absorption", (float*)&absorption, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			ImGui::DragFloat("Factor", (float*)&factor, 0.001f, 0.0f, FLT_MAX, "%.4f");
			ImGui::DragFloat("Scale Height", 
				(float*)&atmosphereSystem->mieScaleHeight, 0.01f, 0.001f, FLT_MAX, "%.3f km");
			ImGui::SliderFloat("Phase G", (float*)&atmosphereSystem->miePhaseG, 0.0f, 1.0f);
			atmosphereSystem->mieScattering = scattering * max(density, 0.000001f);
			atmosphereSystem->mieAbsorption = absorption * max(factor, 0.000001f);
			ImGui::PopID(); ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Ozone"))
		{
			ImGui::PushID("ozone");
			auto factor = length(atmosphereSystem->ozoneAbsorption);
			auto absorption = atmosphereSystem->ozoneAbsorption / (factor == 0.0f ? 1.0f : factor);
			ImGui::ColorEdit3("Absorption", 
				(float*)&absorption, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			ImGui::DragFloat("Factor", (float*)&factor, 0.001f, 0.0f, FLT_MAX, "%.4f");
			ImGui::DragFloat("Layer Width", 
				(float*)&atmosphereSystem->ozoneLayerWidth, 0.01f, 0.0f, FLT_MAX, "%.3f km");
			ImGui::DragFloat("Layer Slope", (float*)&atmosphereSystem->ozoneLayerSlope, 0.01f, 0.0f, FLT_MAX);
			ImGui::DragFloat("Layer Tip", (float*)&atmosphereSystem->ozoneLayerTip, 0.01f, 0.0f, FLT_MAX);
			atmosphereSystem->ozoneAbsorption = absorption * max(factor, 0.000001f);
			ImGui::PopID(); ImGui::Spacing();
		}

		ImGui::DragFloat("Multi Scattering", (float*)&atmosphereSystem->multiScatFactor, 0.01f, 0.0f, FLT_MAX);

		if (ImGui::Button("Set Mars"))
		{
			atmosphereSystem->rayleighScattering = AtmosphereRenderSystem::marsRayleighScattering;
			atmosphereSystem->rayleightScaleHeight = AtmosphereRenderSystem::marsRayleightScaleHeight;
			atmosphereSystem->mieScattering = AtmosphereRenderSystem::marsMieScattering;
			atmosphereSystem->mieScaleHeight = AtmosphereRenderSystem::marsMieScaleHeight;
			atmosphereSystem->mieAbsorption = AtmosphereRenderSystem::marsMieAbsorption;
			atmosphereSystem->miePhaseG = AtmosphereRenderSystem::marsMiePhaseG;
			atmosphereSystem->ozoneAbsorption = AtmosphereRenderSystem::marsOzoneAbsorption;
			atmosphereSystem->groundAlbedo = AtmosphereRenderSystem::marsGroundAlbedo;
			atmosphereSystem->groundRadius = AtmosphereRenderSystem::marsGroundRadius;
			atmosphereSystem->atmosphereHeight = AtmosphereRenderSystem::marsAtmosphereHeight;
			atmosphereSystem->sunAngularSize = AtmosphereRenderSystem::earthSunAngularSize;
		}
	}
	ImGui::End();
}

void AtmosphereEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Atmosphere (Sky)"))
		showWindow = true;
}
#endif