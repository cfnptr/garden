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

#include "garden/editor/system/render/skybox.hpp"

#if GARDEN_EDITOR
using namespace garden;

//--------------------------------------------------------------------------------------------------
SkyboxEditor::SkyboxEditor(SkyboxRenderSystem* system)
{
	EditorRenderSystem::getInstance()->registerEntityInspector(typeid(SkyboxRenderComponent),
		[this](ID<Entity> entity) { onEntityInspector(entity); }); // TODO: set inspectorPriority = -0.1f
	this->system = system;
}
// TODO: unregister inspector

//--------------------------------------------------------------------------------------------------
void SkyboxEditor::onEntityInspector(ID<Entity> entity)
{
	if (ImGui::CollapsingHeader("Skybox Render"))
	{
		auto manager = Manager::getInstance();
		auto graphicsSystem = system->getGraphicsSystem();
		auto skyboxView = manager->get<SkyboxRenderComponent>(entity);

		if (skyboxView->cubemap)
		{
			auto imageView = graphicsSystem->get(skyboxView->cubemap);
			auto stringOffset = imageView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos)
				stringOffset = 0;
			else
				stringOffset++;
			auto image = to_string(*skyboxView->cubemap) + " (" +
				string(imageView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("Cubemap", &image, ImGuiInputTextFlags_ReadOnly);
		}
		else
		{
			ImGui::Text("Cubemap: null");
		}

		if (skyboxView->descriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(skyboxView->descriptorSet);
			auto stringOffset = descriptorSetView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos)
				stringOffset = 0;
			else
				stringOffset++;
			auto descriptorSet = to_string(*skyboxView->descriptorSet) + " (" +
				string(descriptorSetView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("Descriptor Set", &descriptorSet, ImGuiInputTextFlags_ReadOnly);
		}
		else
		{
			ImGui::Text("Descriptor Set: null");
		}
	}
}
#endif