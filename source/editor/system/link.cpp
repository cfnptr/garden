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
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", LinkEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", LinkEditorSystem::editorBarTool);

	EditorRenderSystem::getInstance()->registerEntityInspector<LinkComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void LinkEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<LinkComponent>();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", LinkEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", LinkEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderUuidList()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	auto linkSystem = LinkSystem::getInstance();
	const auto& uuidMap = linkSystem->getUuidMap();

	for (const auto& pair : uuidMap)
	{
		auto name = pair.first.toBase64();
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				auto entity = linkSystem->findEntity(pair.first);
				EditorRenderSystem::getInstance()->selectedEntity = entity;
			}
		}
	}

	if (uuidMap.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No linked UUID");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}
static void renderTagList()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	auto linkSystem = LinkSystem::getInstance();
	const auto& tagMap = linkSystem->getTagMap();
	map<string, uint32> uniqueTags;

	for (const auto& pair : tagMap)
	{
		auto searchResult = uniqueTags.find(pair.first);
		if (searchResult == uniqueTags.end())
		{
			uniqueTags.emplace(pair.first, 1);
			continue;
		}
		searchResult->second++;
	}

	for (const auto& pair : uniqueTags)
	{
		const auto& name = pair.first + " [" + to_string(pair.second) + "]";
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	if (tagMap.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No linked tag");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

void LinkEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 256.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Link Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		// TODO: add UUID and tag search box, also multithread search for big entity ammount if needed.
		if (ImGui::CollapsingHeader("UUID List"))
			renderUuidList();
		if (ImGui::CollapsingHeader("Tag List"))
			renderTagList();
	}
	ImGui::End();
}
void LinkEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Link Viewer"))
		showWindow = true;
}

//**********************************************************************************************************************
void LinkEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto linkComponent = Manager::getInstance()->get<LinkComponent>(entity);
		ImGui::Text("UUID: %s, Tag: %s", linkComponent->getUUID().toBase64().c_str(),
			linkComponent->getTag().c_str());
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto linkComponent = Manager::getInstance()->get<LinkComponent>(entity);
	auto tag = linkComponent->getTag();
	if (ImGui::InputText("Tag", &tag))
		linkComponent->setTag(tag);

	auto uuid = linkComponent->getUUID().toBase64();
	if (ImGui::InputText("UUID", &uuid))
	{
		uuid.resize(22);
		auto hash = Hash128();
		if (hash.fromBase64(uuid))
			linkComponent->trySetUUID(hash);
	}
	if (ImGui::BeginPopupContextItem("uuid"))
	{
		if (ImGui::MenuItem("Reset Default"))
			linkComponent->trySetUUID({});
		if (ImGui::MenuItem("Generate Random"))
			linkComponent->regenerateUUID();
		ImGui::EndPopup();
	}
}
#endif