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

#include "garden/system/graphics/editor/lighting.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
LightingEditor::LightingEditor(LightingRenderSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerEntityInspector(typeid(LightingRenderComponent),
		[this](ID<Entity> entity) { onEntityInspector(entity); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void LightingEditor::onEntityInspector(ID<Entity> entity)
{
	ImGui::PushID("LightingRenderComponent");
	if (ImGui::CollapsingHeader("Lighting Render"))
	{
		auto manager = system->getManager();
		auto graphicsSystem = system->getGraphicsSystem();
		auto lightingComponent = manager->get<LightingRenderComponent>(entity);

		if (lightingComponent->cubemap)
		{
			auto imageView = graphicsSystem->get(lightingComponent->cubemap);
			auto stringOffset = imageView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
			auto image = to_string(*lightingComponent->cubemap) + " (" +
				string(imageView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("Cubemap", &image, ImGuiInputTextFlags_ReadOnly);
		}
		else ImGui::Text("Cubemap: null");

		if (lightingComponent->sh)
		{
			auto bufferView = graphicsSystem->get(lightingComponent->sh);
			auto stringOffset = bufferView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
			auto buffer = to_string(*lightingComponent->sh) + " (" +
				string(bufferView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("SH", &buffer, ImGuiInputTextFlags_ReadOnly);
		}
		else ImGui::Text("SH: null");

		if (lightingComponent->specular)
		{
			auto imageView = graphicsSystem->get(lightingComponent->specular);
			auto stringOffset = imageView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
			ImGui::Text("Specular: %d (%s)", *lightingComponent->specular,
				imageView->getDebugName().c_str() + stringOffset);
			auto image = to_string(*lightingComponent->specular) + " (" +
				string(imageView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("Specular", &image, ImGuiInputTextFlags_ReadOnly);
		}
		else ImGui::Text("Cubemap: null");

		if (lightingComponent->descriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(lightingComponent->descriptorSet);
			auto stringOffset = descriptorSetView->getDebugName().find_last_of('.');
			if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
			auto descriptorSet = to_string(*lightingComponent->descriptorSet) + " (" +
				string(descriptorSetView->getDebugName().c_str() + stringOffset) + ")";
			ImGui::InputText("Descriptor Set", &descriptorSet, ImGuiInputTextFlags_ReadOnly);
		}
		else ImGui::Text("Descriptor Set: null");
		
		ImGui::Spacing();
	}
	ImGui::PopID();
}
#endif