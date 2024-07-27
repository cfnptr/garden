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

#include "garden/editor/system/spawn.hpp"

#if GARDEN_EDITOR
#include "garden/system/link.hpp"
#include "garden/system/spawn.hpp"
#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
SpawnEditorSystem::SpawnEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", SpawnEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", SpawnEditorSystem::deinit);
}
SpawnEditorSystem::~SpawnEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", SpawnEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", SpawnEditorSystem::deinit);
	}
}

void SpawnEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", SpawnEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", SpawnEditorSystem::editorBarTool);

	EditorRenderSystem::getInstance()->registerEntityInspector<SpawnComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void SpawnEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<SpawnComponent>();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", SpawnEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", SpawnEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderSpawns(const string& searchString, bool searchCaseSensitive)
{
	auto linkSystem = LinkSystem::getInstance();
	auto spawnSystem = SpawnSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& components = spawnSystem->getComponents();
	auto occupancy = components.getOccupancy();
	auto componentData = components.getData();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (uint32 i = 0; i < occupancy; i++)
	{
		auto spawnView = &componentData[i];
		if (!spawnView->getEntity())
			continue;

		if (!searchString.empty())
		{
			if (!find(spawnView->path.generic_string(), searchString, searchCaseSensitive) &&
				!find(spawnView->prefab.toBase64(), searchString, searchCaseSensitive))
			{
				continue;
			}
		}

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == spawnView->getEntity())
			flags |= ImGuiTreeNodeFlags_Selected;

		auto name = spawnView->path.empty() ? spawnView->prefab.toBase64() :
			spawnView->path.generic_string() + " (" + spawnView->prefab.toBase64() + ")";
		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				editorSystem->selectedEntity = spawnView->getEntity();
			ImGui::TreePop();
		}
	}

	if (components.getCount() == 0)
	{
		ImGui::Indent();
		ImGui::TextDisabled("No spawns");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderSharedPrefabs(const string& searchString, bool searchCaseSensitive)
{
	auto linkSystem = LinkSystem::getInstance();
	auto spawnSystem = SpawnSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& sharedPrefabs = spawnSystem->getSharedPrefabs();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& pair : sharedPrefabs)
	{
		if (!searchString.empty())
		{
			if (!find(pair.first, searchString, searchCaseSensitive) &&
				!find(pair.second.toBase64(), searchString, searchCaseSensitive))
			{
				continue;
			}
		}

		auto entity = linkSystem->findEntity(pair.second);

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		auto name = pair.first + " (" + pair.second.toBase64() + ")";
		if (!entity)
			name += " [Destroyed]";

		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				editorSystem->selectedEntity = entity;
			ImGui::TreePop();
		}
	}

	if (sharedPrefabs.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No shared prefabs");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
void SpawnEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 256.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Spawn Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		ImGui::InputText("Search", &searchString); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive); ImGui::Spacing();

		if (ImGui::CollapsingHeader("Spawns"))
			renderSpawns(searchString, searchCaseSensitive);
		if (ImGui::CollapsingHeader("Shared Prefabs"))
			renderSharedPrefabs(searchString, searchCaseSensitive);
		ImGui::Spacing();

		if (ImGui::Button("Destroy Shared Prefabs", ImVec2(-FLT_MIN, 0.0f)))
			SpawnSystem::getInstance()->destroySharedPrefabs();
	}
	ImGui::End();
}
void SpawnEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Spawn Viewer"))
		showWindow = true;
}

//**********************************************************************************************************************
static void renderSpawnedEntities(const vector<Hash128>& spawnedEntities)
{
	auto linkSystem = LinkSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->findEntity(uuid);

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		auto name = uuid.toBase64();
		if (!entity)
			name += " [Destroyed]";

		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				editorSystem->selectedEntity = entity;
			ImGui::TreePop();
		}
	}

	if (spawnedEntities.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No spawned entities");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
void SpawnEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto spawnView = SpawnSystem::getInstance()->get(entity);
		ImGui::Text("Active: %s, Path: %s, Prefab: %s", spawnView->isActive ? "true" : "false",
			spawnView->path.generic_string().c_str(), spawnView->prefab.toBase64().c_str());
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto spawnView = SpawnSystem::getInstance()->get(entity);
	ImGui::Checkbox("Active", &spawnView->isActive);
	
	static const set<string> extensions = { ".scene" };
	EditorRenderSystem::getInstance()->drawFileSelector(spawnView->path, 
		spawnView->getEntity(), typeid(SpawnComponent), "scenes", extensions);
	
	auto uuid = spawnView->prefab.toBase64();
	if (ImGui::InputText("Prefab", &uuid))
	{
		auto prefab = spawnView->prefab;
		if (prefab.fromBase64(uuid))
			spawnView->prefab = prefab;
	}
	if (ImGui::BeginPopupContextItem("prefab"))
	{
		if (ImGui::MenuItem("Reset Default"))
			spawnView->prefab = {};
		ImGui::EndPopup();
	}

	auto maxCount = (int)spawnView->maxCount;
	if (ImGui::DragInt("Max Count", &maxCount))
		spawnView->maxCount = (uint32)std::min(maxCount, 0);
	ImGui::DragFloat("Delay", &spawnView->delay);

	const auto modes = "One Shot\00";
	ImGui::Combo("Mode", &spawnView->mode, modes);	

	if (ImGui::CollapsingHeader("Spawned Entities"))
		renderSpawnedEntities(spawnView->getSpawnedEntities());
	ImGui::Spacing();

	if (ImGui::Button("Destroy Spawned", ImVec2(-FLT_MIN, 0.0f)))
		spawnView->destroySpawned();
}
#endif