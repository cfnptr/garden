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

#include "garden/system/editor/transform.hpp"

#if GARDEN_EDITOR
#include "garden/system/graphics/editor.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
TransformEditor::TransformEditor(TransformSystem* system)
{
	auto manager = system->getManager();
	auto editorSystem = manager->tryGet<EditorRenderSystem>();
	if (editorSystem)
	{
		editorSystem->registerEntityInspector(typeid(TransformComponent),
			[this](ID<Entity> entity) { onEntityInspector(entity); });
	}
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void TransformEditor::onDestroy(ID<Entity> entity)
{
	auto manager = system->getManager();
	auto editorSystem = manager->tryGet<EditorRenderSystem>();
	if (editorSystem)
	{
		if (editorSystem->selectedEntity == entity) editorSystem->selectedEntity = {};
	}
}

//--------------------------------------------------------------------------------------------------
void TransformEditor::onEntityInspector(ID<Entity> entity)
{
	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	ImGui::PushID("TransformComponent");

	if (ImGui::CollapsingHeader("Transform"))
	{
		auto transformComponent = manager->get<TransformComponent>(entity);
		ImGui::BeginDisabled(manager->has<BakedTransformComponent>(entity));

		ImGui::DragFloat3("Position", (float*)&transformComponent->position, 0.01f);
		if (ImGui::BeginItemTooltip())
		{
			auto translation = getTranslation(transformComponent->calcModel());
			ImGui::Text("Position of the entity\nGlobal: %.1f, %.1f, %.1f",
				translation.x, translation.y, translation.z);
			ImGui::EndTooltip();
		}

		ImGui::DragFloat3("Scale", (float*)&transformComponent->scale,
			0.01f, 0.0001f, FLT_MAX);
		if (ImGui::BeginItemTooltip())
		{
			auto scale = extractScale(transformComponent->calcModel());
			ImGui::Text("Scale of the entity\nGlobal: %.3f, %.3f, %.3f",
				scale.x, scale.y, scale.z);
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
			auto rotation = radians(newEulerAngles);
			auto global = degrees(extractQuat(extractRotation(
				transformComponent->calcModel())).toEulerAngles());
			ImGui::Text("Rotation in degrees\nRadians: "
				"%.3f, %.3f, %.3f\nGlobal: %.1f, %.1f, %.1f",
				rotation.x, rotation.y, rotation.z, global.x, global.y, global.z);
			ImGui::EndTooltip();
		}

		ImGui::EndDisabled();
		
		ImGui::InputText("Name", &transformComponent->name);
		ImGui::Spacing(); ImGui::Separator();
		
		if (transformComponent->getParent())
		{
			auto parentTransform = manager->get<TransformComponent>(
				transformComponent->getParent());
			ImGui::Text("Parent: %d (%s) | Childs: %d", *transformComponent->getParent(),
				parentTransform->name.c_str(), transformComponent->getChildCount());
		}
		else
		{
			ImGui::Text("Parent: null | Childs: %d",
				transformComponent->getChildCount());
		}

		if (transformComponent->getParent())
		{
			if (ImGui::Button("Select Parent"))
				editorSystem->selectedEntity = transformComponent->getParent(); 
			ImGui::SameLine();
			if (ImGui::Button("Remove Parent")) transformComponent->setParent({});
		}

		ImGui::Spacing();
	}

	if (editorSystem->selectedEntity != selectedEntity)
	{
		if (editorSystem->selectedEntity)
		{
			auto transformComponent = manager->get<TransformComponent>(
				editorSystem->selectedEntity);
			oldEulerAngles = newEulerAngles = degrees(
				transformComponent->rotation.toEulerAngles());
			oldRotation = transformComponent->rotation;
		}

		selectedEntity = editorSystem->selectedEntity;
	}
	else
	{
		if (editorSystem->selectedEntity)
		{
			auto transformComponent = manager->get<TransformComponent>(
				editorSystem->selectedEntity);
			if (oldRotation != transformComponent->rotation)
			{
				oldEulerAngles = newEulerAngles = degrees(
					transformComponent->rotation.toEulerAngles());
				oldRotation = transformComponent->rotation;
			}
		}
	}

	ImGui::PopID();
}
#endif