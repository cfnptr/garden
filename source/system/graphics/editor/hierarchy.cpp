//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/graphics/editor/hierarchy.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
HierarchyEditor::HierarchyEditor(EditorRenderSystem* system)
{
	auto manager = system->getManager();
	if (manager->has<TransformSystem>())
		system->registerBarTool([this]() { onBarTool(); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
static void updateHierarchyClick(Manager* manager, TransformComponent* transform)
{
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		auto graphicsSystem = manager->get<GraphicsSystem>();
		auto editorSystem = manager->get<EditorRenderSystem>();
		editorSystem->selectedEntity = transform->getEntity();
		editorSystem->selectedEntityAabb = Aabb();

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && graphicsSystem->camera)
		{
			auto cameraTransform = manager->get<TransformComponent>(
				graphicsSystem->camera);
			auto model = transform->calcModel();
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
			newTransform->setParent(transform->getEntity());
		}

		if (!manager->has<DoNotDestroyComponent>(transform->getEntity()))
		{
			if (ImGui::MenuItem("Destroy Entity"))
				manager->destroy(transform->getEntity());

			if (ImGui::MenuItem("Destroy Entities"))
			{
				auto transformSystem = manager->get<TransformSystem>();
				transformSystem->destroyRecursive(transform->getEntity());
			}
		}
		
		ImGui::EndPopup();
	}

	// TODO: scroll window when dragging.
	// allow to drop on empty window space.
	// allow to drop between elements.
	// On selecting entity in scene open hierarchy view to it.

	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			auto entity = *(const ID<Entity>*)(payload->Data);
			// TODO: detect if entity is destroyed. Or clear payload on destroy.
			auto entityTransform = manager->get<TransformComponent>(entity);
			if (transform)
			{
				if (!transform->hasAncestor(entity))
					entityTransform->setParent(transform->getEntity());
			}	
			else entityTransform->setParent({});
		}
		ImGui::EndDragDropTarget();
	}
	if (transform && !transform->hasBaked() && ImGui::BeginDragDropSource())
	{
		auto entity = transform->getEntity();
		ImGui::SetDragDropPayload("Entity", &entity, sizeof(ID<Entity>));
		ImGui::Text("%s", transform->name.c_str());
		ImGui::EndDragDropSource();
	}
}
static void renderHierarchyEntity(Manager* manager,
	TransformComponent* transform, ID<Entity> selectedEntity)
{
	auto flags = (int)ImGuiTreeNodeFlags_OpenOnArrow;
	if (transform->getEntity() == selectedEntity) flags |= ImGuiTreeNodeFlags_Selected;
	if (transform->getChildCount() == 0) flags |= ImGuiTreeNodeFlags_Leaf;
	
	if (ImGui::TreeNodeEx(transform->name.c_str(), flags))
	{
		updateHierarchyClick(manager, transform);
		for (uint32 i = 0; i < transform->getChildCount(); i++)
		{
			renderHierarchyEntity(manager, *manager->get<TransformComponent>(
				transform->getChilds()[i]), selectedEntity);
		}
		ImGui::TreePop();
	}
	else updateHierarchyClick(manager, transform);
}
static bool findCaseInsensitive(const string& haystack, const string& needle)
{
	auto it = search(haystack.begin(), haystack.end(),
		needle.begin(), needle.end(), [](char a, char b)
	{
		return toupper(a) == toupper(b);
	});
	return (it != haystack.end() );
}

//--------------------------------------------------------------------------------------------------
void HierarchyEditor::render()
{
	if (showWindow)
	{
		if (ImGui::Begin("Entity Hierarchy", &showWindow,
			ImGuiWindowFlags_NoFocusOnAppearing))
		{
			auto manager = system->getManager();
			auto transformSystem = manager->get<TransformSystem>();
			auto& components = transformSystem->getComponents();
			auto componentData = (TransformComponent*)components.getData();
			auto componentOccupancy = components.getOccupancy();
		
			ImGui::InputText("Search", &hierarchySearch); ImGui::SameLine();
			ImGui::Checkbox("Aa", &hierarchyCaseSensitive);
			ImGui::Separator();

			ImGui::PushStyleColor(ImGuiCol_Header,
				ImGui::GetStyle().Colors[ImGuiCol_Button]);

			if (hierarchySearch.empty())
			{
				for (uint32 i = 0; i < componentOccupancy; i++)
				{
					auto transform = &componentData[i];
					if (!transform->getEntity() || transform->getParent()) continue;
					renderHierarchyEntity(manager, transform, system->selectedEntity);
				}
			}
			else
			{
				for (uint32 i = 0; i < componentOccupancy; i++)
				{
					auto transform = &componentData[i];
					if (!transform->getEntity()) continue;

					if (hierarchyCaseSensitive)
					{
						if (transform->name.find(hierarchySearch) == string::npos)
							continue;
					}
					else
					{
						if (!findCaseInsensitive(transform->name, hierarchySearch))
							continue;
					}

					auto flags = (int)(ImGuiTreeNodeFlags_OpenOnArrow |
						ImGuiTreeNodeFlags_Leaf);
					if (transform->getEntity() == system->selectedEntity)
						flags |= ImGuiTreeNodeFlags_Selected;
					if (ImGui::TreeNodeEx(transform->name.c_str(), flags))
					{
						updateHierarchyClick(manager, transform);
						ImGui::TreePop();
					}
				}
			}

			ImGui::PopStyleColor();
		}
		ImGui::End();
	}
}

void HierarchyEditor::onBarTool()
{
	if (ImGui::MenuItem("Entity Hierarchy")) showWindow = true;
}
#endif