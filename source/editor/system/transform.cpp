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

#if GARDEN_EDITOR
#include "garden/system/transform.hpp"
#include "math/angles.hpp"

#include "imgui_internal.h"

using namespace garden;

//**********************************************************************************************************************
TransformEditorSystem* TransformEditorSystem::instance = nullptr;

TransformEditorSystem::TransformEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", TransformEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", TransformEditorSystem::deinit);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
TransformEditorSystem::~TransformEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", TransformEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", TransformEditorSystem::deinit);
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void TransformEditorSystem::init()
{
	GARDEN_ASSERT(Manager::getInstance()->has<EditorRenderSystem>());
	EditorRenderSystem::getInstance()->registerEntityInspector<TransformComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void TransformEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<TransformComponent>();
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
	auto manager = Manager::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();

	auto transformComponent = manager->get<TransformComponent>(entity);
	if (ImGui::BeginItemTooltip())
	{
		if (transformComponent->getParent())
		{
			auto parentTransform = manager->get<TransformComponent>(transformComponent->getParent());
			ImGui::Text("Parent: %lu %s", (unsigned long)*transformComponent->getParent(),
				parentTransform->debugName.empty() ? "" : ("(" + parentTransform->debugName + ")").c_str());
		}
		else
		{
			ImGui::Text("Parent: null");
		}
		ImGui::Text("Children count: %lu", (unsigned long)transformComponent->getChildCount());
		ImGui::EndTooltip();
	}

	if (isOpened)
	{
		ImGui::Checkbox("Active", &transformComponent->isActive);

		auto isBaked = manager->has<BakedTransformComponent>(entity);
		ImGui::BeginDisabled(isBaked);

		ImGui::DragFloat3("Position", &transformComponent->position, 0.01f);
		if (ImGui::BeginPopupContextItem("position"))
		{
			if (ImGui::MenuItem("Reset Default"))
				transformComponent->position = float3(0.0f);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			if (isBaked)
				ImGui::Text("Disabled due to BakedTransformComponent!");
			auto translation = getTranslation(transformComponent->calcModel());
			ImGui::Text("Position of the entity\nGlobal: %.1f, %.1f, %.1f",
				translation.x, translation.y, translation.z);
			ImGui::EndTooltip();
		}

		ImGui::DragFloat3("Scale", &transformComponent->scale, 0.01f, 0.0001f, FLT_MAX);
		if (ImGui::BeginPopupContextItem("scale"))
		{
			if (ImGui::MenuItem("Reset Default"))
				transformComponent->scale = float3(1.0f);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			if (isBaked)
				ImGui::Text("Disabled due to BakedTransformComponent!");
			auto scale = extractScale(transformComponent->calcModel());
			ImGui::Text("Scale of the entity\nGlobal: %.3f, %.3f, %.3f", scale.x, scale.y, scale.z);
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f))
		{
			auto difference = newEulerAngles - oldEulerAngles;
			transformComponent->rotation *= quat(radians(difference));
			oldEulerAngles = newEulerAngles;
		}
		if (ImGui::BeginPopupContextItem("rotation"))
		{
			if (ImGui::MenuItem("Reset Default"))
				transformComponent->rotation = quat::identity;
			ImGui::EndPopup();
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
		
		ImGui::InputText("Debug Name", &transformComponent->debugName);
		if (ImGui::BeginPopupContextItem("debugName"))
		{
			if (ImGui::MenuItem("Copy Name"))
				ImGui::SetClipboardText(transformComponent->debugName.c_str());
			if (ImGui::MenuItem("Paste Name"))
				transformComponent->debugName = ImGui::GetClipboardText();
			if (ImGui::MenuItem("Clear Name"))
				transformComponent->debugName = "";
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Entity debug only name");
			ImGui::EndTooltip();
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