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
#include "garden/system/transform.hpp"
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
	},
	inspectorPriority);
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
static void renderUuidList(const string& searchString, bool searchCaseSensitive)
{
	auto linkSystem = LinkSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& uuidMap = linkSystem->getUuidMap();
	const auto& linkComponents = linkSystem->getComponents();

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& pair : uuidMap)
	{
		auto uuid = pair.first.toBase64();
		if (!searchString.empty())
		{
			if (!find(uuid, searchString, searchCaseSensitive))
				continue;
		}

		auto linkView = linkComponents.get(pair.second);
		auto entity = linkView->getEntity();

		auto flags = (int)ImGuiTreeNodeFlags_Leaf;
		if (editorSystem->selectedEntity == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (ImGui::TreeNodeEx(uuid.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				editorSystem->selectedEntity = entity;
			ImGui::TreePop();
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

//**********************************************************************************************************************
static void renderTagList(const string& searchString, bool searchCaseSensitive)
{
	auto manager = Manager::getInstance();
	auto linkSystem = LinkSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& tagMap = linkSystem->getTagMap();
	const auto& linkComponents = linkSystem->getComponents();
	map<string, uint32> uniqueTags;

	for (const auto& pair : tagMap)
	{
		if (!searchString.empty())
		{
			if (!find(pair.first, searchString, searchCaseSensitive))
				continue;
		}

		auto searchResult = uniqueTags.find(pair.first);
		if (searchResult == uniqueTags.end())
		{
			uniqueTags.emplace(pair.first, 1);
			continue;
		}
		searchResult->second++;
	}

	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	for (const auto& pair : uniqueTags)
	{
		const auto& tag = pair.first + " [" + to_string(pair.second) + "]";
		if (ImGui::TreeNodeEx(tag.c_str()))
		{
			auto range = tagMap.equal_range(pair.first);
			for (auto i = range.first; i != range.second; i++)
			{
				auto linkView = linkComponents.get(i->second);
				auto entity = linkView->getEntity();
				auto transformView = manager->tryGet<TransformComponent>(entity);
				auto name = transformView && transformView->debugName.empty() ?
					"Entity " + to_string(*entity) : transformView->debugName;

				auto flags = (int)ImGuiTreeNodeFlags_Leaf;
				if (editorSystem->selectedEntity == entity)
					flags |= ImGuiTreeNodeFlags_Selected;

				ImGui::PushID(to_string(*entity).c_str());
				if (ImGui::TreeNodeEx(name.c_str(), flags))
				{
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
						editorSystem->selectedEntity = entity;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			ImGui::TreePop();
		}
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

//**********************************************************************************************************************
void LinkEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 256.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Link Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		ImGui::InputText("Search", &searchString); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive); ImGui::Spacing();

		if (ImGui::CollapsingHeader("UUID List"))
			renderUuidList(searchString, searchCaseSensitive);
		if (ImGui::CollapsingHeader("Tag List"))
			renderTagList(searchString, searchCaseSensitive);
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
		auto linkView = LinkSystem::getInstance()->get(entity);
		ImGui::Text("UUID: %s, Tag: %s", linkView->getUUID().toBase64().c_str(),
			linkView->getTag().c_str());
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto linkView = LinkSystem::getInstance()->get(entity);
	auto tag = linkView->getTag();
	if (ImGui::InputText("Tag", &tag))
		linkView->setTag(tag);

	auto uuid = linkView->getUUID().toBase64();
	if (ImGui::InputText("UUID", &uuid))
	{
		auto hash = linkView->getUUID();
		if (hash.fromBase64(uuid))
			linkView->trySetUUID(hash);
	}
	if (ImGui::BeginPopupContextItem("uuid"))
	{
		if (ImGui::MenuItem("Reset Default"))
			linkView->trySetUUID({});
		if (ImGui::MenuItem("Generate Random"))
			linkView->regenerateUUID();
		ImGui::EndPopup();
	}
}
#endif