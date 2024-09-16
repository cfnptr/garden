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

#include "garden/editor/system/render/lighting.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/lighting.hpp"

using namespace garden;

//**********************************************************************************************************************
LightingRenderEditorSystem::LightingRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", LightingRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", LightingRenderEditorSystem::deinit);
}
LightingRenderEditorSystem::~LightingRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", LightingRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", LightingRenderEditorSystem::deinit);
	}
}

void LightingRenderEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<LightingRenderComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void LightingRenderEditorSystem::deinit()
{
	EditorRenderSystem::Instance::get()->unregisterEntityInspector<LightingRenderComponent>();
}

//**********************************************************************************************************************
void LightingRenderEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto lightingView = manager->get<LightingRenderComponent>(entity);

	if (lightingView->cubemap) // TODO: use common resource name gui shower.
	{
		auto imageView = graphicsSystem->get(lightingView->cubemap);
		auto stringOffset = imageView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos)
			stringOffset = 0;
		else
			stringOffset++;
		auto image = to_string(*lightingView->cubemap) + " (" +
			string(imageView->getDebugName().c_str() + stringOffset) + ")";
		ImGui::InputText("Cubemap", &image, ImGuiInputTextFlags_ReadOnly);
	}
	else
	{
		ImGui::Text("Cubemap: null");
	}

	if (lightingView->sh)
	{
		auto bufferView = graphicsSystem->get(lightingView->sh);
		auto stringOffset = bufferView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos)
			stringOffset = 0;
		else
			stringOffset++;
		auto buffer = to_string(*lightingView->sh) + " (" +
			string(bufferView->getDebugName().c_str() + stringOffset) + ")";
		ImGui::InputText("SH", &buffer, ImGuiInputTextFlags_ReadOnly);
	}
	else
	{
		ImGui::Text("SH: null");
	}

	if (lightingView->specular)
	{
		auto imageView = graphicsSystem->get(lightingView->specular);
		auto stringOffset = imageView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos)
			stringOffset = 0;
		else
			stringOffset++;
		ImGui::Text("Specular: %lu (%s)", (unsigned long)*lightingView->specular,
			imageView->getDebugName().c_str() + stringOffset);
		auto image = to_string(*lightingView->specular) + " (" +
			string(imageView->getDebugName().c_str() + stringOffset) + ")";
		ImGui::InputText("Specular", &image, ImGuiInputTextFlags_ReadOnly);
	}
	else
	{
		ImGui::Text("Cubemap: null");
	}

	if (lightingView->descriptorSet)
	{
		auto descriptorSetView = graphicsSystem->get(lightingView->descriptorSet);
		auto stringOffset = descriptorSetView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos)
			stringOffset = 0;
		else
			stringOffset++;
		auto descriptorSet = to_string(*lightingView->descriptorSet) + " (" +
			string(descriptorSetView->getDebugName().c_str() + stringOffset) + ")";
		ImGui::InputText("Descriptor Set", &descriptorSet, ImGuiInputTextFlags_ReadOnly);
	}
	else
	{
		ImGui::Text("Descriptor Set: null");
	}
}
#endif