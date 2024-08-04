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

#include "garden/editor/system/spawner.hpp"

#if GARDEN_EDITOR
#include "garden/system/link.hpp"
#include "garden/system/spawner.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
SpawnerEditorSystem::SpawnerEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", SpawnerEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", SpawnerEditorSystem::deinit);
}
SpawnerEditorSystem::~SpawnerEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", SpawnerEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", SpawnerEditorSystem::deinit);
	}
}

void SpawnerEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", SpawnerEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", SpawnerEditorSystem::editorBarTool);

	EditorRenderSystem::getInstance()->registerEntityInspector<SpawnerComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void SpawnerEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<SpawnerComponent>();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", SpawnerEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", SpawnerEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderSpawners(const string& searchString, bool searchCaseSensitive)
{
	auto linkSystem = LinkSystem::getInstance();
	auto spawnerSystem = SpawnerSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	auto transformSystem = Manager::getInstance()->tryGet<TransformSystem>();
	const auto& components = spawnerSystem->getComponents();
	auto occupancy = components.getOccupancy();
	auto componentData = components.getData();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (uint32 i = 0; i < occupancy; i++)
	{
		auto spawnerView = &componentData[i];
		if (!spawnerView->getEntity())
			continue;

		auto entity = spawnerView->getEntity();
		if (!searchString.empty())
		{
			if (!find(spawnerView->path.generic_string(), searchString, *entity, searchCaseSensitive) &&
				!find(spawnerView->prefab.toBase64(), searchString, *entity, searchCaseSensitive))
			{
				continue;
			}
		}

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;
		
		string name;
		if (transformSystem && transformSystem->has(entity))
		{
			auto transformView = transformSystem->get(entity);
			name = transformView->debugName.empty() ?
				"Entity " + to_string(*entity) : transformView->debugName;
		}
		else
		{
			name = "Entity " + to_string(*entity);
		}
		if (!spawnerView->path.empty() || spawnerView->prefab)
		{
			name += " (";
			if (!spawnerView->path.empty())
			{
				name += spawnerView->path.generic_string();
				if (spawnerView->prefab)
					name += ", ";
			}
			if (spawnerView->prefab)
				name += spawnerView->prefab.toBase64();
			name += ")";
		}
		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				editorSystem->selectedEntity = entity;
			ImGui::TreePop();
		}
	}

	if (components.getCount() == 0)
	{
		ImGui::Indent();
		ImGui::TextDisabled("No spawners");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderSharedPrefabs(const string& searchString, bool searchCaseSensitive)
{
	auto linkSystem = LinkSystem::getInstance();
	auto spawnerSystem = SpawnerSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& sharedPrefabs = spawnerSystem->getSharedPrefabs();

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
void SpawnerEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 256.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Spawner Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		ImGui::InputText("Search", &searchString); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive); ImGui::Spacing();

		if (ImGui::CollapsingHeader("Spawners"))
			renderSpawners(searchString, searchCaseSensitive);
		if (ImGui::CollapsingHeader("Shared Prefabs"))
			renderSharedPrefabs(searchString, searchCaseSensitive);
		ImGui::Spacing();

		if (ImGui::Button("Destroy Shared Prefabs", ImVec2(-FLT_MIN, 0.0f)))
			SpawnerSystem::getInstance()->destroySharedPrefabs();
	}
	ImGui::End();
}
void SpawnerEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Spawner Viewer"))
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
void SpawnerEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto spawnerView = SpawnerSystem::getInstance()->get(entity);
		ImGui::Text("Active: %s, Path: %s, Prefab: %s",
			spawnerView->isActive ? "true" : "false", spawnerView->path.generic_string().c_str(),
			spawnerView->prefab ? spawnerView->prefab.toBase64().c_str() : "");
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto spawnerView = SpawnerSystem::getInstance()->get(entity);
	ImGui::Checkbox("Active", &spawnerView->isActive);
	
	static const set<string> extensions = { ".scene" };
	EditorRenderSystem::getInstance()->drawFileSelector(spawnerView->path,
		spawnerView->getEntity(), typeid(SpawnerComponent), "scenes", extensions);
	
	auto uuid = spawnerView->prefab ? spawnerView->prefab.toBase64() : "";
	if (ImGui::InputText("Prefab", &uuid))
	{
		auto prefab = spawnerView->prefab;
		if (prefab.fromBase64(uuid))
			spawnerView->prefab = prefab;
	}
	if (ImGui::BeginPopupContextItem("prefab"))
	{
		if (ImGui::MenuItem("Reset Default"))
			spawnerView->prefab = {};
		ImGui::EndPopup();
	}
	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			auto entity = *((const ID<Entity>*)payload->Data);
			auto linkView = LinkSystem::getInstance()->tryGet(entity);
			if (linkView && linkView->getUUID())
				spawnerView->prefab = linkView->getUUID();
		}
		ImGui::EndDragDropTarget();
	}

	auto maxCount = (int)spawnerView->maxCount;
	if (ImGui::DragInt("Max Count", &maxCount))
		spawnerView->maxCount = (uint32)std::min(maxCount, 0);
	ImGui::DragFloat("Delay", &spawnerView->delay);

	const auto modes = "One Shot\00";
	ImGui::Combo("Mode", &spawnerView->mode, modes);

	if (ImGui::CollapsingHeader("Spawned Entities"))
		renderSpawnedEntities(spawnerView->getSpawnedEntities());
	ImGui::Spacing();

	if (ImGui::Button("Destroy Spawned", ImVec2(-FLT_MIN, 0.0f)))
		spawnerView->destroySpawned();
}
#endif