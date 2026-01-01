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

#include "garden/editor/system/spawner.hpp"

#if GARDEN_EDITOR
#include "garden/system/link.hpp"
#include "garden/system/spawner.hpp"
#include "garden/system/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
SpawnerEditorSystem::SpawnerEditorSystem()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", SpawnerEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SpawnerEditorSystem::deinit);
}
SpawnerEditorSystem::~SpawnerEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SpawnerEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SpawnerEditorSystem::deinit);
	}
}

void SpawnerEditorSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", SpawnerEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", SpawnerEditorSystem::editorBarTool);

	EditorRenderSystem::Instance::get()->registerEntityInspector<SpawnerComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void SpawnerEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<SpawnerComponent>();

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", SpawnerEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", SpawnerEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderSpawners(const string& searchString, bool searchCaseSensitive)
{
	auto manager = Manager::Instance::get();
	auto editorSystem = EditorRenderSystem::Instance::get();
	const auto& components = SpawnerSystem::Instance::get()->getComponents();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& spawner : components)
	{
		if (!spawner.getEntity())
			continue;

		auto entity = spawner.getEntity();
		if (!searchString.empty())
		{
			if (!find(spawner.path.generic_string(), searchString, *entity, searchCaseSensitive) &&
				!find(spawner.prefab.toBase64URL(), searchString, *entity, searchCaseSensitive))
			{
				continue;
			}
		}

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		auto transformView = manager->tryGet<TransformComponent>(entity);

		string name;
		if (transformView)
		{
			name = transformView->debugName.empty() ?
				"Entity " + to_string(*entity) : transformView->debugName;
		}
		else
		{
			name = "Entity " + to_string(*entity);
		}

		if (!spawner.path.empty() || spawner.prefab)
		{
			name += " (";
			if (!spawner.path.empty())
			{
				name += spawner.path.generic_string();
				if (spawner.prefab)
					name += ", ";
			}
			if (spawner.prefab)
				name += spawner.prefab.toBase64URL();
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
	auto linkSystem = LinkSystem::Instance::get();
	auto editorSystem = EditorRenderSystem::Instance::get();
	const auto& sharedPrefabs = SpawnerSystem::Instance::get()->getSharedPrefabs();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& pair : sharedPrefabs)
	{
		if (!searchString.empty())
		{
			if (!find(pair.first, searchString, searchCaseSensitive) &&
				!find(pair.second.toBase64URL(), searchString, searchCaseSensitive))
			{
				continue;
			}
		}

		auto entity = linkSystem->tryGet(pair.second);

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		auto name = pair.first + " (" + pair.second.toBase64URL() + ")";
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
void SpawnerEditorSystem::preUiRender()
{
	if (!showWindow)
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
			SpawnerSystem::Instance::get()->destroySharedPrefabs();
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
	auto linkSystem = LinkSystem::Instance::get();
	auto editorSystem = EditorRenderSystem::Instance::get();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->tryGet(uuid);

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		auto name = uuid.toBase64URL();
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
		auto spawnerView = Manager::Instance::get()->get<SpawnerComponent>(entity);
		ImGui::Text("Active: %s, Path: %s, Prefab: %s",
			spawnerView->isActive ? "true" : "false", spawnerView->path.generic_string().c_str(),
			spawnerView->prefab ? spawnerView->prefab.toBase64URL().c_str() : "");
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto spawnerView = Manager::Instance::get()->get<SpawnerComponent>(entity);
	ImGui::Checkbox("Active", &spawnerView->isActive); ImGui::SameLine();
	ImGui::Checkbox("Spawn As Child", &spawnerView->spawnAsChild);
	
	static const vector<string_view> extensions = { ".scene" };
	EditorRenderSystem::Instance::get()->drawFileSelector("Prefab", spawnerView->path,
		spawnerView->getEntity(), typeid(SpawnerComponent), "scenes", extensions);
	
	auto uuid = spawnerView->prefab ? spawnerView->prefab.toBase64URL() : "";
	if (ImGui::InputText("UUID", &uuid))
	{
		auto prefab = spawnerView->prefab;
		if (prefab.fromBase64URL(uuid))
			spawnerView->prefab = prefab;
	}
	if (ImGui::BeginPopupContextItem("uuid"))
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
			GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
			auto entity = *((const ID<Entity>*)payload->Data);
			auto linkView = Manager::Instance::get()->tryGet<LinkComponent>(entity);
			if (linkView && linkView->getUUID())
				spawnerView->prefab = linkView->getUUID();
		}
		ImGui::EndDragDropTarget();
	}

	auto maxCount = (int)spawnerView->maxCount;
	if (ImGui::DragInt("Max Count", &maxCount))
		spawnerView->maxCount = (uint32)std::max(maxCount, 0);
	ImGui::DragFloat("Delay", &spawnerView->delay, 1.0f, 0.0f, 0.0f, "%.3f s");
	ImGui::Combo("Mode", spawnerView->mode, spawnModeNames, (int)SpawnMode::Count);

	if (ImGui::CollapsingHeader("Spawned Entities"))
		renderSpawnedEntities(spawnerView->getSpawnedEntities());
	ImGui::Spacing();

	if (ImGui::Button("Destroy Spawned", ImVec2(-FLT_MIN, 0.0f)))
		spawnerView->destroySpawned();
}
#endif