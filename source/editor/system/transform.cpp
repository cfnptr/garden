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

#include "garden/editor/system/transform.hpp"
#include "math/angles.hpp"

#if GARDEN_EDITOR
#include "imgui_internal.h"

using namespace garden;

//**********************************************************************************************************************
TransformEditorSystem::TransformEditorSystem(Manager* manager, TransformSystem* system) : EditorSystem(manager, system)
{
	EditorRenderSystem::getInstance()->registerEntityInspector<TransformComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}

//**********************************************************************************************************************
void TransformEditorSystem::onEntityDestroy(ID<Entity> entity)
{
	if (EditorRenderSystem::getInstance()->selectedEntity == entity)
		EditorRenderSystem::getInstance()->selectedEntity = {};

	auto payload = ImGui::GetDragDropPayload();
	if (payload)
	{
		auto payloadEntity = *((const ID<Entity>*)payload->Data);
		if (payloadEntity == entity)
			ImGui::ClearDragDrop();
	}
}

//**********************************************************************************************************************
void TransformEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto manager = getManager();
	auto editorSystem = EditorRenderSystem::getInstance();

	if (isOpened)
	{
		auto transformComponent = manager->get<TransformComponent>(entity);
		auto isBaked = manager->has<BakedTransformComponent>(entity);
		ImGui::BeginDisabled(isBaked);

		ImGui::DragFloat3("Position", (float*)&transformComponent->position, 0.01f);
		if (ImGui::BeginItemTooltip())
		{
			if (isBaked)
				ImGui::Text("Disabled due to BakedTransformComponent!");
			auto translation = getTranslation(transformComponent->calcModel());
			ImGui::Text("Position of the entity\nGlobal: %.1f, %.1f, %.1f",
				translation.x, translation.y, translation.z);
			ImGui::EndTooltip();
		}

		ImGui::DragFloat3("Scale", (float*)&transformComponent->scale, 0.01f, 0.0001f, FLT_MAX);
		if (ImGui::BeginItemTooltip())
		{
			if (isBaked)
				ImGui::Text("Disabled due to BakedTransformComponent!");
			auto scale = extractScale(transformComponent->calcModel());
			ImGui::Text("Scale of the entity\nGlobal: %.3f, %.3f, %.3f", scale.x, scale.y, scale.z);
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat3("Rotation", (float*)&newEulerAngles, 0.3f))
		{
			auto difference = newEulerAngles - oldEulerAngles;
			transformComponent->rotation *= quat(radians(difference));
			oldEulerAngles = newEulerAngles;
		}
		if (ImGui::BeginItemTooltip())
		{
			if (isBaked)
				ImGui::Text("Disabled due to BakedTransformComponent!");
			auto rotation = radians(newEulerAngles);
			auto global = degrees(extractQuat(extractRotation(transformComponent->calcModel())).toEulerAngles());
			ImGui::Text("Rotation in degrees\nRadians: %.3f, %.3f, %.3f\nGlobal: %.1f, %.1f, %.1f",
				rotation.x, rotation.y, rotation.z, global.x, global.y, global.z);
			ImGui::EndTooltip();
		}

		ImGui::EndDisabled();
		
		ImGui::InputText("Name", &transformComponent->name);
		ImGui::Spacing(); ImGui::Separator();
		
		if (transformComponent->getParent())
		{
			auto parentTransform = manager->get<TransformComponent>(transformComponent->getParent());
			ImGui::Text("Parent: %lu (%s) | Childs: %lu", *transformComponent->getParent(),
				parentTransform->name.c_str(), transformComponent->getChildCount());
		}
		else
		{
			ImGui::Text("Parent: null | Childs: %lu", transformComponent->getChildCount());
		}

		if (transformComponent->getParent())
		{
			if (ImGui::Button("Select Parent"))
				editorSystem->selectedEntity = transformComponent->getParent(); 
			ImGui::SameLine();
			if (ImGui::Button("Remove Parent"))
				transformComponent->setParent({});
		}
	}

	if (editorSystem->selectedEntity != selectedEntity)
	{
		if (editorSystem->selectedEntity)
		{
			auto transformComponent = manager->get<TransformComponent>(editorSystem->selectedEntity);
			oldEulerAngles = newEulerAngles = degrees(transformComponent->rotation.toEulerAngles());
			oldRotation = transformComponent->rotation;
		}

		selectedEntity = editorSystem->selectedEntity;
	}
	else
	{
		if (editorSystem->selectedEntity)
		{
			auto transformComponent = manager->get<TransformComponent>(editorSystem->selectedEntity);
			if (oldRotation != transformComponent->rotation)
			{
				oldEulerAngles = newEulerAngles = degrees(transformComponent->rotation.toEulerAngles());
				oldRotation = transformComponent->rotation;
			}
		}
	}
}
#endif