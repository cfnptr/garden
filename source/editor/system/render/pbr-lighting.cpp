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

#include "garden/editor/system/render/pbr-lighting.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/pbr-lighting.hpp"

using namespace garden;

//**********************************************************************************************************************
PbrLightingRenderEditorSystem::PbrLightingRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", PbrLightingRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PbrLightingRenderEditorSystem::deinit);
}
PbrLightingRenderEditorSystem::~PbrLightingRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingRenderEditorSystem::deinit);
	}
}

void PbrLightingRenderEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<PbrLightingRenderComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void PbrLightingRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<PbrLightingRenderComponent>();
}

//**********************************************************************************************************************
void PbrLightingRenderEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto pbrLightingView = PbrLightingRenderSystem::Instance::get()->getComponent(entity);
	auto editorSystem = EditorRenderSystem::Instance::get();
	editorSystem->drawResource(pbrLightingView->cubemap, "Cubemap");
	editorSystem->drawResource(pbrLightingView->sh, "SH");
	editorSystem->drawResource(pbrLightingView->specular, "Speculare");
	editorSystem->drawResource(pbrLightingView->descriptorSet);

	// TODO: allow to select cubemap from file
}
#endif