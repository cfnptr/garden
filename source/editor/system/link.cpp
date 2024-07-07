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

#include "garden/editor/system/link.hpp"

#if GARDEN_EDITOR
#include "garden/system/link.hpp"

using namespace garden;

//**********************************************************************************************************************
LinkEditorSystem::LinkEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", LinkEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", LinkEditorSystem::deinit);
}
LinkEditorSystem::~LinkEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", LinkEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", LinkEditorSystem::deinit);
	}
}

void LinkEditorSystem::init()
{
	GARDEN_ASSERT(Manager::getInstance()->has<EditorRenderSystem>());
	EditorRenderSystem::getInstance()->registerEntityInspector<LinkComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void LinkEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<LinkComponent>();
}

//**********************************************************************************************************************
void LinkEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto linkComponent = Manager::getInstance()->get<LinkComponent>(entity);
	auto uuid = linkComponent->getUUID().toBase64();
	ImGui::InputText("UUID", &uuid, ImGuiInputTextFlags_ReadOnly);

	if (ImGui::Button("Regenerate UUID", ImVec2(-FLT_MIN, 0.0f)))
		LinkSystem::getInstance()->regenerateUUID(linkComponent);
}
#endif