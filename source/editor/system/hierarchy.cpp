// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/graphics.hpp"
#include "garden/system/transform.hpp"
#include "math/matrix/transform.hpp"

using namespace garden;

// TODO: render lines for the hierarchy entities, for better visual.

//**********************************************************************************************************************
HierarchyEditorSystem::HierarchyEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", HierarchyEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", HierarchyEditorSystem::deinit);
}
HierarchyEditorSystem::~HierarchyEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", HierarchyEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", HierarchyEditorSystem::deinit);
	}
}

void HierarchyEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", HierarchyEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", HierarchyEditorSystem::editorBarTool);
}
void HierarchyEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", HierarchyEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", HierarchyEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void updateHierarchyClick(ID<Entity> renderEntity)
{
	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
		EditorRenderSystem::Instance::get()->selectedEntity = renderEntity;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
		graphicsSystem->camera && graphicsSystem->camera != renderEntity)
	{
		auto entityTransformView = transformSystem->tryGetComponent(renderEntity);
		auto cameraTransformView = transformSystem->tryGetComponent(graphicsSystem->camera);
		if (entityTransformView && cameraTransformView)
		{
			auto position = getTranslation(entityTransformView->calcModel());
			auto cameraView = manager->tryGet<CameraComponent>(graphicsSystem->camera);

			if (cameraView)
			{
				position = cameraView->type == ProjectionType::Perspective ? 
					position + f32x4(0.0f, 0.0f, -2.0f) * cameraTransformView->getRotation() :
					f32x4(position.getX(), position.getY(), -0.5f);
			}

			cameraTransformView->setPosition(position);
		}
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Create Entity"))
		{
			auto entity = manager->createEntity();
			if (manager->has<TransformComponent>(renderEntity))
			{
				auto newTransformView = manager->add<TransformComponent>(entity);
				newTransformView->setParent(renderEntity);
			}
			EditorRenderSystem::Instance::get()->selectedEntity = entity;
		}
		if (ImGui::MenuItem("Duplicate Entity", nullptr, false, !manager->has<DoNotDuplicateComponent>(renderEntity)))
		{
			auto duplicate = transformSystem->duplicateRecursive(renderEntity);
			auto entityTransformView = transformSystem->tryGetComponent(renderEntity);
			if (entityTransformView)
			{
				auto duplicateTransformView = transformSystem->getComponent(duplicate);
				duplicateTransformView->setParent(entityTransformView->getParent());
				duplicateTransformView->debugName += " " + to_string(*duplicate);
			}
			EditorRenderSystem::Instance::get()->selectedEntity = duplicate;
		}
		if (ImGui::MenuItem("Destroy Entity", nullptr, false, !manager->has<DoNotDestroyComponent>(renderEntity)))
		{
			transformSystem->destroyRecursive(renderEntity);
			ImGui::EndPopup();
			return;
		}
		auto hasTransform = manager->has<TransformComponent>(renderEntity);
		if (ImGui::MenuItem("Copy Debug Name", nullptr, false, hasTransform))
		{
			auto transformView = transformSystem->getComponent(renderEntity);
			auto debugName = transformView->debugName.empty() ?
				"Entity " + to_string(*renderEntity) : transformView->debugName;
			ImGui::SetClipboardText(debugName.c_str());
		}
		if (ImGui::MenuItem("Store as Scene", nullptr, false, hasTransform))
		{
			auto editorSystem = EditorRenderSystem::Instance::get();
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
			GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
			auto entity = *((const ID<Entity>*)payload->Data);
			auto entityTransformView = transformSystem->tryGetComponent(entity);
			if (entityTransformView)
			{
				auto renderTransformView = transformSystem->tryGetComponent(renderEntity);
				if (renderTransformView && !renderTransformView->hasAncestor(entity))
					entityTransformView->setParent(renderEntity);
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("Entity", &renderEntity, sizeof(ID<Entity>));
		auto renderTransformView = transformSystem->tryGetComponent(renderEntity);
		if (renderTransformView && !renderTransformView->debugName.empty())
		{
			ImGui::Text("%s", renderTransformView->debugName.c_str());
		}
		else
		{
			auto debugName = "Entity " + to_string(*renderEntity);
			ImGui::Text("%s", debugName.c_str());
		}
		ImGui::EndDragDropSource();
	}
}

//**********************************************************************************************************************
static void renderHierarchyEntity(ID<Entity> renderEntity, ID<Entity> selectedEntity)
{
	auto transformView = TransformSystem::Instance::get()->getComponent(renderEntity);
	auto debugName = transformView->debugName.empty() ? 
		"Entity " + to_string(*renderEntity) : transformView->debugName;
	
	auto flags = (int)(ImGuiTreeNodeFlags_OpenOnArrow);
	if (transformView->getEntity() == selectedEntity)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (transformView->getChildCount() == 0)
		flags |= ImGuiTreeNodeFlags_Leaf;
	
	ImGui::PushID(to_string(*renderEntity).c_str());

	if (!transformView->isActive())
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

	if (ImGui::TreeNodeEx(debugName.c_str(), flags))
	{
		if (!transformView->isActive())
			ImGui::PopStyleColor();
		updateHierarchyClick(renderEntity);

		transformView = TransformSystem::Instance::get()->getComponent(renderEntity); // Do not optimize!!!
		for (uint32 i = 0; i < transformView->getChildCount(); i++)
		{
			renderHierarchyEntity(transformView->getChild(i), selectedEntity); // TODO: use stack instead of recursion!
			transformView = TransformSystem::Instance::get()->getComponent(renderEntity); // Do not optimize!!!
		}
		ImGui::TreePop();
	}
	else
	{
		if (!transformView->isActive())
			ImGui::PopStyleColor();
		updateHierarchyClick(renderEntity);
	}
	ImGui::PopID();
}

//**********************************************************************************************************************
void HierarchyEditorSystem::preUiRender()
{
	if (!showWindow || !TransformSystem::Instance::has())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 192.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Entity Hierarchy", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		auto editorSystem = EditorRenderSystem::Instance::get();
		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Entity"))
			{
				auto manager = Manager::Instance::get();
				auto entity = manager->createEntity();
				auto transformView = manager->add<TransformComponent>(entity);
				if (GraphicsSystem::Instance::get()->camera)
				{
					const auto& cameraConstants = GraphicsSystem::Instance::get()->getCameraConstants();
					transformView->setPosition(cameraConstants.cameraPos + cameraConstants.viewDir);
				}
				editorSystem->selectedEntity = entity;
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
				GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
				auto entity = *((const ID<Entity>*)payload->Data);
				auto entityTransform = TransformSystem::Instance::get()->tryGetComponent(entity);
				if (entityTransform)
					entityTransform->setParent({});
			}
			ImGui::EndDragDropTarget();
		}
		
		ImGui::InputText("Search", &searchString); ImGui::SameLine();
		ImGui::Checkbox("Aa", &searchCaseSensitive);
		ImGui::Spacing(); ImGui::Separator();

		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		const auto& components = TransformSystem::Instance::get()->getComponents();
		if (searchString.empty())
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Note: Do not optimize occupancy!!!
			{
				auto transformView = &((const TransformComponent*)components.getData())[i];
				if (!transformView->getEntity() || transformView->getParent())
					continue;
				renderHierarchyEntity(transformView->getEntity(), editorSystem->selectedEntity);
			}
		}
		else
		{
			for (uint32 i = 0; i < components.getOccupancy(); i++) // Note: Do not optimize occupancy!!!
			{
				auto transformView = &((const TransformComponent*)components.getData())[i];
				if (!transformView->getEntity())
					continue;

				auto debugName = transformView->debugName.empty() ?
					"Entity " + to_string(*transformView->getEntity()) : transformView->debugName;
				if (!find(debugName, searchString, *transformView->getEntity(), searchCaseSensitive))
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

		const auto& entities = Manager::Instance::get()->getEntities();
		auto hasSeparator = false;

		// Note: Entities without transform component.
		for (uint32 i = 0; i < entities.getOccupancy(); i++)  // Note: Do not optimize occupancy!!!
		{
			auto entityView = &(entities.getData()[i]);
			if (!entityView->hasComponents() || entityView->findComponent(typeid(TransformComponent).hash_code()))
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
				updateHierarchyClick(entities.getID(entityView));
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