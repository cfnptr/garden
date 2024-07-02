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

#include "garden/editor/system/hierarchy.hpp"

#if GARDEN_EDITOR
#include "garden/system/transform.hpp"

using namespace garden;

// TODO: render lines for the hierarchy entities, for better visual.

//**********************************************************************************************************************
HierarchyEditorSystem::HierarchyEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", HierarchyEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", HierarchyEditorSystem::deinit);
}
HierarchyEditorSystem::~HierarchyEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", HierarchyEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", HierarchyEditorSystem::deinit);
	}
}

void HierarchyEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());
	
	SUBSCRIBE_TO_EVENT("EditorRender", HierarchyEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", HierarchyEditorSystem::editorBarTool);
}
void HierarchyEditorSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", HierarchyEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", HierarchyEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void updateHierarchyClick(ID<Entity> renderEntity)
{
	auto manager = Manager::getInstance();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		auto editorSystem = EditorRenderSystem::getInstance();
		editorSystem->selectedEntity = renderEntity;
		editorSystem->selectedEntityAabb = Aabb();

		auto graphicsSystem = GraphicsSystem::getInstance();
		if (graphicsSystem->camera && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			auto entityTransform = manager->get<TransformComponent>(renderEntity);
			auto cameraTransform = manager->get<TransformComponent>(graphicsSystem->camera);
			auto model = entityTransform->calcModel();
			auto offset = float3(0.0f, 0.0f, -2.0f) * cameraTransform->rotation;
			cameraTransform->position = getTranslation(model) + offset;
		}
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Create Entity"))
		{
			auto entity = manager->createEntity();
			auto newTransform = manager->add<TransformComponent>(entity);
			newTransform->setParent(renderEntity);
			ImGui::SetNextItemOpen(true);
		}

		ImGui::BeginDisabled(manager->has<DoNotDuplicateComponent>(renderEntity));
		if (ImGui::MenuItem("Duplicate Entity"))
			manager->duplicate(renderEntity);
		if (ImGui::MenuItem("Duplicate Entities"))
			TransformSystem::getInstance()->duplicateRecursive(renderEntity);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(manager->has<DoNotDestroyComponent>(renderEntity));
		if (ImGui::MenuItem("Destroy Entity"))
			manager->destroy(renderEntity);
		if (ImGui::MenuItem("Destroy Entities"))
			TransformSystem::getInstance()->destroyRecursive(renderEntity);
		ImGui::EndDisabled();

		if (ImGui::MenuItem("Copy Debug Name"))
		{
			auto transform = manager->get<TransformComponent>(renderEntity);
			auto debugName = transform->debugName.empty() ?
				"Entity " + to_string(*renderEntity) : transform->debugName;
			ImGui::SetClipboardText(debugName.c_str());
		}
		if (ImGui::MenuItem("Store as Scene"))
		{
			auto editorSystem = EditorRenderSystem::getInstance();
			editorSystem->selectedEntity = renderEntity;
			editorSystem->exportScene = true;
		}
		
		ImGui::EndPopup();
	}

	// TODO:
	// allow to drop between elements.
	// On selecting entity in scene open hierarchy view to it.

	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			auto entity = *((const ID<Entity>*)payload->Data);
			auto entityTransform = manager->get<TransformComponent>(entity);
			if (renderEntity)
			{
				auto renderTransform = manager->get<TransformComponent>(renderEntity);
				if (!renderTransform->hasAncestor(entity))
					entityTransform->setParent(renderEntity);
			}	
			else
			{
				entityTransform->setParent({});
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (renderEntity)
	{
		auto renderTransform = manager->get<TransformComponent>(renderEntity);
		if (!renderTransform->hasBakedWithDescendants() && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("Entity", &renderEntity, sizeof(ID<Entity>));
			if (renderTransform->debugName.empty())
			{
				auto debugName = "Entity " + to_string(*renderEntity);
				ImGui::Text("%s", debugName.c_str());
			}
			else
			{
				ImGui::Text("%s", renderTransform->debugName.c_str());
			}
			ImGui::EndDragDropSource();
		}
	}
}

//**********************************************************************************************************************
static void renderHierarchyEntity(ID<Entity> renderEntity, ID<Entity> selectedEntity)
{
	auto transform = Manager::getInstance()->get<TransformComponent>(renderEntity);
	auto debugName = transform->debugName.empty() ? "Entity " + to_string(*renderEntity) : transform->debugName;
	
	auto flags = (int)(ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);
	if (transform->getEntity() == selectedEntity)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (transform->getChildCount() == 0)
		flags |= ImGuiTreeNodeFlags_Leaf;
	
	ImGui::PushID(to_string(*renderEntity).c_str());
	if (ImGui::TreeNodeEx(debugName.c_str(), flags))
	{
		updateHierarchyClick(renderEntity);

		transform = Manager::getInstance()->get<TransformComponent>(renderEntity); // Do not optimize!!!
		for (uint32 i = 0; i < transform->getChildCount(); i++)
		{
			renderHierarchyEntity(transform->getChilds()[i], selectedEntity); // TODO: use stack instead of recursion!
			transform = Manager::getInstance()->get<TransformComponent>(renderEntity); // Do not optimize!!!
		}
		ImGui::TreePop();
	}
	else
	{
		updateHierarchyClick(renderEntity);
	}
	ImGui::PopID();
}

//**********************************************************************************************************************
void HierarchyEditorSystem::editorRender()
{
	auto manager = Manager::getInstance();
	if (!showWindow || !GraphicsSystem::getInstance()->canRender() || !manager->has<TransformSystem>())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 192.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Entity Hierarchy", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Entity"))
			{
				auto entity = manager->createEntity();
				manager->add<TransformComponent>(entity);
			}
			ImGui::EndPopup();
		}

		auto cursorPos = ImGui::GetCursorScreenPos();
		auto regionAvail = ImGui::GetContentRegionAvail();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetScrollY());
		ImGui::Dummy(regionAvail);
		ImGui::SetCursorScreenPos(cursorPos);

		if (ImGui::BeginDragDropTarget())
		{
			auto mousePos = ImGui::GetMousePos();
			auto containerPos = ImGui::GetItemRectMin();
			auto containerSize = ImGui::GetItemRectSize();
			const auto hotZoneHeight = 12.0f, scrollSpeed = 1.0f;

			if (mousePos.y - containerPos.y < hotZoneHeight)
				ImGui::SetScrollY(ImGui::GetScrollY() - scrollSpeed);
			if ((containerPos.y + containerSize.y) - mousePos.y < hotZoneHeight)
				ImGui::SetScrollY(ImGui::GetScrollY() + scrollSpeed);
			// TODO: adjust speed based on cursor to edge distance?

			auto payload = ImGui::AcceptDragDropPayload("Entity");
			if (payload)
			{
				auto entity = *((const ID<Entity>*)payload->Data);
				auto entityTransform = manager->get<TransformComponent>(entity);
				entityTransform->setParent({});
			}
			ImGui::EndDragDropTarget();
		}
		
		ImGui::InputText("Search", &hierarchySearch); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive);
		ImGui::Spacing(); ImGui::Separator();

		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto editorSystem = EditorRenderSystem::getInstance();
		const auto& components = TransformSystem::getInstance()->getComponents();

		if (hierarchySearch.empty())
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Do not optimize occupancy!!!
			{
				auto transform = &((const TransformComponent*)components.getData())[i];
				if (!transform->getEntity() || transform->getParent())
					continue;
				renderHierarchyEntity(transform->getEntity(), editorSystem->selectedEntity);
			}
		}
		else
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Do not optimize occupancy!!!
			{
				auto transform = &((const TransformComponent*)components.getData())[i];
				if (!transform->getEntity())
					continue;

				auto debugName = transform->debugName.empty() ?
					"Entity " + to_string(*transform->getEntity()) : transform->debugName;
				if (!hierarchySearch.empty())
				{
					if (!find(debugName, hierarchySearch, searchCaseSensitive))
						continue;
				}

				auto flags = (int)ImGuiTreeNodeFlags_Leaf;
				if (transform->getEntity() == editorSystem->selectedEntity)
					flags |= ImGuiTreeNodeFlags_Selected;
					
				if (ImGui::TreeNodeEx(debugName.c_str(), flags))
				{
					updateHierarchyClick(transform->getEntity());
					ImGui::TreePop();
				}
			}
		}

		const auto& entities = manager->getEntities();
		auto entityData = entities.getData();
		auto entityOccupancy = entities.getOccupancy();
		auto hasSeparator = false;

		for (uint32 i = 0; i < entityOccupancy; i++) // Entities without transform component
		{
			auto entity = &entityData[i];
			const auto& components = entity->getComponents();

			if (components.empty() || components.find(typeid(TransformComponent)) != components.end())
				continue;

			if (!hasSeparator)
			{
				ImGui::Separator();
				hasSeparator = true;
			}
			
			auto flags = (int)ImGuiTreeNodeFlags_Leaf;
			if (entities.getID(entity) == editorSystem->selectedEntity)
				flags |= ImGuiTreeNodeFlags_Selected;
			auto debugName = "Entity " + to_string(*entities.getID(entity));

			if (ImGui::TreeNodeEx(debugName.c_str(), flags))
			{
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					editorSystem->selectedEntity = entities.getID(entity);
					editorSystem->selectedEntityAabb = Aabb();
				}
				ImGui::TreePop();
			}
		}

		ImGui::PopStyleColor();
	}
	ImGui::End();
}

//**********************************************************************************************************************
void HierarchyEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Entity Hierarchy"))
		showWindow = true;
}
#endif