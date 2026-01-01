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

#include "garden/editor/system/network.hpp"

#if GARDEN_EDITOR
#include "garden/system/network.hpp"

using namespace garden;

//**********************************************************************************************************************
NetworkEditorSystem::NetworkEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", NetworkEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", NetworkEditorSystem::deinit);
}
NetworkEditorSystem::~NetworkEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", NetworkEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", NetworkEditorSystem::deinit);
	}
}

void NetworkEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<NetworkComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void NetworkEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<NetworkComponent>();
}

//**********************************************************************************************************************
void NetworkEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto networkView = Manager::Instance::get()->get<NetworkComponent>(entity);
		ImGui::Text("Client Owned: %s", networkView->isClientOwned ? "true" : "false");
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto networkView = Manager::Instance::get()->get<NetworkComponent>(entity);
	auto uid = to_string(networkView->getEntityUID());
	if (ImGui::InputText("Entity UID", &uid))
		networkView->trySetEntityUID(strtoul(uid.c_str(), nullptr, 10));
	
	uid = networkView->getClientUID() ? networkView->getClientUID() : "";
	if (ImGui::InputText("Client UID", &uid))
		networkView->setClientUID(uid);
	ImGui::Checkbox("Client Owned", &networkView->isClientOwned);
}
#endif