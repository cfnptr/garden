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

#include "garden/editor/system/render/atmosphere.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/atmosphere.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

//**********************************************************************************************************************
AtmosphereEditorSystem::AtmosphereEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", AtmosphereEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AtmosphereEditorSystem::deinit);
}
AtmosphereEditorSystem::~AtmosphereEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AtmosphereEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AtmosphereEditorSystem::deinit);
	}
}

void AtmosphereEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", AtmosphereEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", AtmosphereEditorSystem::editorBarToolPP);
}
void AtmosphereEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", AtmosphereEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", AtmosphereEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void AtmosphereEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Sky Atmosphere", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto atmosphereSystem = AtmosphereRenderSystem::Instance::get();
		ImGui::Checkbox("Enabled", &atmosphereSystem->isEnabled);
		ImGui::SameLine();

		ImGui::Checkbox("No-Delay Mode", &atmosphereSystem->noDelay);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Render all skybox faces and compute SH in one frame. (Expensive!)");
			ImGui::EndTooltip();
		}

		auto quality = atmosphereSystem->getQuality();
		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			atmosphereSystem->setQuality(quality);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("atmosphere.quality", toString((GraphicsQuality)quality));
		}
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Planet"))
		{
			ImGui::DragFloat("Atmosphere Height", &atmosphereSystem->atmosphereHeight, 
				0.1f, 0.001f, FLT_MAX, "%.3f km");
			ImGui::DragFloat("Ground Radius", &atmosphereSystem->groundRadius, 10.0f, 0.001f, FLT_MAX, "%.3f km");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Mean radius of the planet in kilometres.");
				ImGui::EndTooltip();
			}
			ImGui::SliderFloat3("Grounds Albedo", (float*)&atmosphereSystem->groundAlbedo, 0.0f, 1.0f);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Fraction of incoming star light reflected back into space.");
				ImGui::EndTooltip();
			}
			ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Star"))
		{
			ImGui::PushID("star");
			ImGui::DragFloat("Angular Size", &atmosphereSystem->starAngularSize, 0.01f, 0.0f, 360.0f, "%.3f°");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Angular diameter of nearby star in the sky as seen from planet.");
				ImGui::EndTooltip();
			}
			ImGui::ColorEdit3("Color", (float*)&atmosphereSystem->starColor);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Artistic color of the star. (Does not impact light scattering!)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Luminance", &atmosphereSystem->starColor.w, 100.0f, 0.0f, FLT_MAX);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Luminance of the nearby star. (Energy)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("GI Factor", &atmosphereSystem->giFactor, 0.01f, 0.0f, FLT_MAX);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Artistic global illumination light multiplier.");
				ImGui::EndTooltip();
			}
			ImGui::PopID(); ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Rayleigh"))
		{
			ImGui::PushID("rayleigh");
			auto density = length(atmosphereSystem->rayleighScattering);
			auto scattering = atmosphereSystem->rayleighScattering / (density == 0.0f ? 1.0f : density);
			ImGui::ColorEdit3("Scattering", (float*)&scattering, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Light scattering by a small particles. (Molecules like N2, O2)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Density", &density, 0.001f, 0.0f, FLT_MAX, "%.4f");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Small particles layer density, Rayleigh scattering strength.");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Layer Height", &atmosphereSystem->rayleightScaleHeight, 
				0.01f, 0.001f, FLT_MAX, "%.3f km");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Small particles layer height in kilometres.");
				ImGui::EndTooltip();
			}
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
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Light scattering by a big particles. (Dust, smoke, water droplets)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Density", &density, 0.001f, 0.0f, FLT_MAX, "%.4f");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Big particles layer density, Mie scattering strength.");
				ImGui::EndTooltip();
			}
			ImGui::ColorEdit3("Absorption", (float*)&absorption, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Light absorption by a big particles. (Dust, smoke, water droplets)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Factor", &factor, 0.001f, 0.0f, FLT_MAX, "%.4f");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Light absorption strength by a big particles.");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Layer Height", &atmosphereSystem->mieScaleHeight, 0.01f, 0.001f, FLT_MAX, "%.3f km");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Big particles layer height in kilometres.");
				ImGui::EndTooltip();
			}
			ImGui::SliderFloat("Phase G", &atmosphereSystem->miePhaseG, 0.0f, 1.0f);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Shape of the Mie phase function. (Directionality)");
				ImGui::EndTooltip();
			}
			atmosphereSystem->mieScattering = scattering * max(density, 0.000001f);
			atmosphereSystem->mieAbsorption = absorption * max(factor, 0.000001f);
			ImGui::PopID(); ImGui::Spacing();
		}
		if (ImGui::CollapsingHeader("Ozone"))
		{
			ImGui::PushID("ozone");
			auto factor = length(atmosphereSystem->ozoneAbsorption);
			auto absorption = atmosphereSystem->ozoneAbsorption / (factor == 0.0f ? 1.0f : factor);
			ImGui::ColorEdit3("Absorption", (float*)&absorption, 
				ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Ozone layer light absorption. (Affects twilight sky color)");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Factor", &factor, 0.001f, 0.0f, FLT_MAX, "%.4f");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Ozone layer light absorption strength.");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Layer Height", &atmosphereSystem->ozoneLayerWidth, 0.01f, 0.0f, FLT_MAX, "%.3f km");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Ozone layer height in kilometres.");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Layer Slope", &atmosphereSystem->ozoneLayerSlope, 0.01f, 0.0f, FLT_MAX);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Ozone layer slope factor.");
				ImGui::EndTooltip();
			}
			ImGui::DragFloat("Layer Tip", &atmosphereSystem->ozoneLayerTip, 0.01f, 0.0f, FLT_MAX);
			if (ImGui::BeginItemTooltip())
			{
				ImGui::Text("Ozone layer tip factor.");
				ImGui::EndTooltip();
			}
			atmosphereSystem->ozoneAbsorption = absorption * max(factor, 0.000001f);
			ImGui::PopID(); ImGui::Spacing();
		}

		ImGui::DragFloat("Multi-Scattering", &atmosphereSystem->multiScatFactor, 0.01f, 0.0f, FLT_MAX);
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Atmosphere multi-scattering strength.");
			ImGui::EndTooltip();
		}

		if (ImGui::Button("Earth"))
		{
			atmosphereSystem->rayleighScattering = AtmosphereRenderSystem::earthRayleighScattering;
			atmosphereSystem->rayleightScaleHeight = AtmosphereRenderSystem::earthRayleightScaleHeight;
			atmosphereSystem->mieScattering = AtmosphereRenderSystem::earthMieScattering;
			atmosphereSystem->mieScaleHeight = AtmosphereRenderSystem::earthMieScaleHeight;
			atmosphereSystem->mieAbsorption = AtmosphereRenderSystem::earthMieAbsorption;
			atmosphereSystem->miePhaseG = AtmosphereRenderSystem::earthMiePhaseG;
			atmosphereSystem->ozoneAbsorption = AtmosphereRenderSystem::earthOzoneAbsorption;
			atmosphereSystem->ozoneLayerWidth = AtmosphereRenderSystem::earthOzoneLayerWidth;
			atmosphereSystem->ozoneLayerSlope = AtmosphereRenderSystem::earthOzoneLayerSlope;
			atmosphereSystem->ozoneLayerTip = AtmosphereRenderSystem::earthOzoneLayerTip;
			atmosphereSystem->groundAlbedo = AtmosphereRenderSystem::earthGroundAlbedo;
			atmosphereSystem->groundRadius = AtmosphereRenderSystem::earthGroundRadius;
			atmosphereSystem->atmosphereHeight = AtmosphereRenderSystem::earthAtmosphereHeight;
			atmosphereSystem->starAngularSize = AtmosphereRenderSystem::earthSunAngularSize;
		} ImGui::SameLine();
		if (ImGui::Button("Mars"))
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
			atmosphereSystem->starAngularSize = AtmosphereRenderSystem::marsSunAngularSize;
		}
	}
	ImGui::End();
}

void AtmosphereEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("Sky Atmosphere"))
		showWindow = true;
}
#endif