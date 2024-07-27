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
#include "garden/system/camera.hpp"
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
	auto transformSystem = TransformSystem::getInstance();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		auto editorSystem = EditorRenderSystem::getInstance();
		editorSystem->selectedEntity = renderEntity;

		auto graphicsSystem = GraphicsSystem::getInstance();
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			graphicsSystem->camera && graphicsSystem->camera != renderEntity)
		{
			auto entityTransformView = transformSystem->get(renderEntity);
			auto cameraTransformView = transformSystem->get(graphicsSystem->camera);
			auto model = entityTransformView->calcModel();
			cameraTransformView->position = getTranslation(model);

			auto cameraView = manager->get<CameraComponent>(graphicsSystem->camera);
			if (cameraView)
			{
				if (cameraView->type == ProjectionType::Perspective)
					cameraTransformView->position += float3(0.0f, 0.0f, -2.0f) * cameraTransformView->rotation;
				else
					cameraTransformView->position += float3(0.0f, 0.0f, -0.5f);
			}
		}
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Create Entity"))
		{
			auto entity = manager->createEntity();
			auto newTransformView = manager->add<TransformComponent>(entity);
			newTransformView->setParent(renderEntity);
			ImGui::SetNextItemOpen(true);
		}

		ImGui::BeginDisabled(manager->has<DoNotDuplicateComponent>(renderEntity));
		if (ImGui::MenuItem("Duplicate Entity"))
		{
			auto duplicate = transformSystem->duplicateRecursive(renderEntity);
			auto duplicateTransformView = transformSystem->get(duplicate);
			auto entityTransformView = transformSystem->get(renderEntity);
			duplicateTransformView->setParent(entityTransformView->getParent());
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(manager->has<DoNotDestroyComponent>(renderEntity));
		if (ImGui::MenuItem("Destroy Entity"))
			transformSystem->destroyRecursive(renderEntity);
		ImGui::EndDisabled();

		if (ImGui::MenuItem("Copy Debug Name"))
		{
			auto transformView = transformSystem->get(renderEntity);
			auto debugName = transformView->debugName.empty() ?
				"Entity " + to_string(*renderEntity) : transformView->debugName;
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
			auto entityTransformView = transformSystem->tryGet(entity);
			if (entityTransformView)
			{
				if (renderEntity)
				{
					auto renderTransformView = transformSystem->get(renderEntity);
					if (!renderTransformView->hasAncestor(entity))
						entityTransformView->setParent(renderEntity);
				}
				else
				{
					entityTransformView->setParent({});
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (renderEntity)
	{
		auto renderTransformView = transformSystem->get(renderEntity);
		if (!renderTransformView->hasBakedWithDescendants() && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("Entity", &renderEntity, sizeof(ID<Entity>));
			if (renderTransformView->debugName.empty())
			{
				auto debugName = "Entity " + to_string(*renderEntity);
				ImGui::Text("%s", debugName.c_str());
			}
			else
			{
				ImGui::Text("%s", renderTransformView->debugName.c_str());
			}
			ImGui::EndDragDropSource();
		}
	}
}

//**********************************************************************************************************************
static void renderHierarchyEntity(ID<Entity> renderEntity, ID<Entity> selectedEntity)
{
	auto transformView = TransformSystem::getInstance()->get(renderEntity);
	auto debugName = transformView->debugName.empty() ? 
		"Entity " + to_string(*renderEntity) : transformView->debugName;
	
	auto flags = (int)(ImGuiTreeNodeFlags_OpenOnArrow);
	if (transformView->getEntity() == selectedEntity)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (transformView->getChildCount() == 0)
		flags |= ImGuiTreeNodeFlags_Leaf;
	
	ImGui::PushID(to_string(*renderEntity).c_str());

	if (!transformView->isActive)
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

	if (ImGui::TreeNodeEx(debugName.c_str(), flags))
	{
		if (!transformView->isActive)
			ImGui::PopStyleColor();
		updateHierarchyClick(renderEntity);

		transformView = TransformSystem::getInstance()->get(renderEntity); // Do not optimize!!!
		for (uint32 i = 0; i < transformView->getChildCount(); i++)
		{
			renderHierarchyEntity(transformView->getChilds()[i], selectedEntity); // TODO: use stack instead of recursion!
			transformView = TransformSystem::getInstance()->get(renderEntity); // Do not optimize!!!
		}
		ImGui::TreePop();
	}
	else
	{
		if (!transformView->isActive)
			ImGui::PopStyleColor();
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
				auto entityTransform = TransformSystem::getInstance()->tryGet(entity);
				if (entityTransform)
					entityTransform->setParent({});
			}
			ImGui::EndDragDropTarget();
		}
		
		ImGui::InputText("Search", &searchString); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive);
		ImGui::Spacing(); ImGui::Separator();

		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto editorSystem = EditorRenderSystem::getInstance();
		const auto& components = TransformSystem::getInstance()->getComponents();

		if (searchString.empty())
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Do not optimize occupancy!!!
			{
				auto transformView = &((const TransformComponent*)components.getData())[i];
				if (!transformView->getEntity() || transformView->getParent())
					continue;
				renderHierarchyEntity(transformView->getEntity(), editorSystem->selectedEntity);
			}
		}
		else
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Do not optimize occupancy!!!
			{
				auto transformView = &((const TransformComponent*)components.getData())[i];
				if (!transformView->getEntity())
					continue;

				auto debugName = transformView->debugName.empty() ?
					"Entity " + to_string(*transformView->getEntity()) : transformView->debugName;
				if (!find(debugName, searchString, searchCaseSensitive))
					continue;

				auto flags = (int)ImGuiTreeNodeFlags_Leaf;
				if (transformView->getEntity() == editorSystem->selectedEntity)
					flags |= ImGuiTreeNodeFlags_Selected;
					
				if (ImGui::TreeNodeEx(debugName.c_str(), flags))
				{
					updateHierarchyClick(transformView->getEntity());
					ImGui::TreePop();
				}
			}
		}

		const auto& entities = manager->getEntities();
		auto hasSeparator = false;

		// Entities without transform component
		for (uint32 i = 0; i < entities.getOccupancy(); i++)  // Do not optimize occupancy!!!
		{
			auto entityView = &(entities.getData()[i]);
			const auto& components = entityView->getComponents();

			if (components.empty() || components.find(typeid(TransformComponent)) != components.end())
				continue;

			if (!hasSeparator)
			{
				ImGui::Separator();
				hasSeparator = true;
			}
			
			auto flags = (int)ImGuiTreeNodeFlags_Leaf;
			if (entities.getID(entityView) == editorSystem->selectedEntity)
				flags |= ImGuiTreeNodeFlags_Selected;
			auto debugName = "Entity " + to_string(*entities.getID(entityView));

			if (ImGui::TreeNodeEx(debugName.c_str(), flags))
			{
				auto entity = entities.getID(entityView);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					editorSystem->selectedEntity = entity;

				if (ImGui::BeginPopupContextItem())
				{
					ImGui::BeginDisabled(manager->has<DoNotDuplicateComponent>(entity));
					if (ImGui::MenuItem("Duplicate Entity"))
						manager->duplicate(entity);
					ImGui::EndDisabled();

					ImGui::BeginDisabled(manager->has<DoNotDestroyComponent>(entity));
					if (ImGui::MenuItem("Destroy Entity"))
						manager->destroy(entity);
					ImGui::EndDisabled();

					ImGui::EndPopup();
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