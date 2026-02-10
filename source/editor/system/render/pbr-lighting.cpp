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

#include "garden/editor/system/render/pbr-lighting.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/settings.hpp"

using namespace garden;

PbrLightingEditorSystem::PbrLightingEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", PbrLightingEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PbrLightingEditorSystem::deinit);
}
PbrLightingEditorSystem::~PbrLightingEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingEditorSystem::deinit);
	}
}

void PbrLightingEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", PbrLightingEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarToolPP", PbrLightingEditorSystem::editorBarToolPP);

	EditorRenderSystem::Instance::get()->registerEntityInspector<PbrLightingComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void PbrLightingEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<PbrLightingComponent>();

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", PbrLightingEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarToolPP", PbrLightingEditorSystem::editorBarToolPP);
	}
}

//**********************************************************************************************************************
void PbrLightingEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("PBR Lighting", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto pbrLightingSystem = PbrLightingSystem::Instance::get();

		auto quality = pbrLightingSystem->getQuality();
		if (ImGui::Combo("Quality", &quality, graphicsQualityNames, (int)GraphicsQuality::Count))
		{
			pbrLightingSystem->setQuality(quality);
			auto settingsSystem = SettingsSystem::Instance::tryGet();
			if (settingsSystem)
				settingsSystem->setString("pbrLighting.quality", toString((GraphicsQuality)quality));
		}

		ImGui::DragFloat("Reflectance Coeff", &pbrLightingSystem->reflectanceCoeff, 0.1f, 0.0f, FLT_MAX);
		ImGui::DragFloat("Blur Sharpness", &pbrLightingSystem->blurSharpness, 0.1f, 0.0f, FLT_MAX);
	}
	ImGui::End();
}

void PbrLightingEditorSystem::editorBarToolPP()
{
	if (ImGui::MenuItem("PBR Lighting"))
		showWindow = true;
}

void PbrLightingEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto pbrLightingView = Manager::Instance::get()->get<PbrLightingComponent>(entity);
	auto editorSystem = EditorRenderSystem::Instance::get();
	editorSystem->drawResource(pbrLightingView->skybox, "Skybox");
	editorSystem->drawResource(pbrLightingView->shDiffuse, "Diffuse SH");
	editorSystem->drawResource(pbrLightingView->specular, "Specular");
	editorSystem->drawResource(pbrLightingView->descriptorSet);

	// TODO: allow to select cubemap from file
}
#endif