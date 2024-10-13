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

#include "garden/editor/system/render/skybox.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/skybox.hpp"

using namespace garden;

//**********************************************************************************************************************
SkyboxRenderEditorSystem::SkyboxRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SkyboxRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SkyboxRenderEditorSystem::deinit);
}
SkyboxRenderEditorSystem::~SkyboxRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning())
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SkyboxRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SkyboxRenderEditorSystem::deinit);
	}
}

void SkyboxRenderEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<SkyboxRenderComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void SkyboxRenderEditorSystem::deinit()
{
	EditorRenderSystem::Instance::get()->unregisterEntityInspector<SkyboxRenderComponent>();
}

//--------------------------------------------------------------------------------------------------
void SkyboxRenderEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto componentView = SkyboxRenderSystem::Instance::get()->getComponent(entity);
	auto editorSystem = EditorRenderSystem::Instance::get();
	editorSystem->drawResource(componentView->cubemap);
	editorSystem->drawResource(componentView->descriptorSet);

	// TODO: use here editorSystem->drawImageSelector() instead.
	// But we need to add cubemaps load support to it.
}
#endif