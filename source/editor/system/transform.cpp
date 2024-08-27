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
	SUBSCRIBE_TO_EVENT("Init", TransformEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", TransformEditorSystem::deinit);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
TransformEditorSystem::~TransformEditorSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", TransformEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", TransformEditorSystem::deinit);
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void TransformEditorSystem::init()
{
	EditorRenderSystem::get()->registerEntityInspector<TransformComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void TransformEditorSystem::deinit()
{
	EditorRenderSystem::get()->unregisterEntityInspector<TransformComponent>();
}

//**********************************************************************************************************************
void TransformEditorSystem::onEntityDestroy(ID<Entity> entity)
{
	if (EditorRenderSystem::get()->selectedEntity == entity)
		EditorRenderSystem::get()->selectedEntity = {};

	auto payload = ImGui::GetDragDropPayload();
	if (payload)
	{
		GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
		auto payloadEntity = *((const ID<Entity>*)payload->Data);
		if (payloadEntity == entity)
			ImGui::ClearDragDrop();
	}
}

//**********************************************************************************************************************
void TransformEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto manager = Manager::get();
	auto transformSystem = TransformSystem::get();

	if (ImGui::BeginItemTooltip())
	{
		auto transformView = transformSystem->get(entity);
		ImGui::Text("Active: %s", transformView->isActive ? "true" : "false");
		if (transformView->getParent())
		{
			auto parentView = transformSystem->get(transformView->getParent());
			ImGui::Text("Parent: %lu %s", (unsigned long)*transformView->getParent(),
				parentView->debugName.empty() ? "" : ("(" + parentView->debugName + ")").c_str());
		}
		if (transformView->getChildCount() > 0)
			ImGui::Text("Children count: %lu", (unsigned long)transformView->getChildCount());
		ImGui::EndTooltip();
	}

	if (entity != selectedEntity)
	{
		if (entity)
		{
			auto transformView = transformSystem->get(entity);
			oldEulerAngles = newEulerAngles = degrees(transformView->rotation.toEulerAngles());
			oldRotation = transformView->rotation;
		}

		selectedEntity = entity;
	}
	else
	{
		if (entity)
		{
			auto transformView = transformSystem->get(entity);
			if (oldRotation != transformView->rotation)
			{
				oldEulerAngles = newEulerAngles = degrees(transformView->rotation.toEulerAngles());
				oldRotation = transformView->rotation;
			}
		}
	}

	if (!isOpened)
		return;

	auto transformView = transformSystem->get(entity);
	ImGui::Checkbox("Active", &transformView->isActive);

	auto isBaked = manager->has<BakedTransformComponent>(entity);
	ImGui::BeginDisabled(isBaked);

	ImGui::DragFloat3("Position", &transformView->position, 0.01f);
	if (ImGui::BeginPopupContextItem("position"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->position = float3(0.0f);
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isBaked)
			ImGui::Text("Disabled due to BakedTransformComponent!");
		auto translation = getTranslation(transformView->calcModel());
		ImGui::Text("Position of the entity\nGlobal: %.1f, %.1f, %.1f",
			translation.x, translation.y, translation.z);
		ImGui::EndTooltip();
	}

	ImGui::DragFloat3("Scale", &transformView->scale, 0.01f, 0.0001f, FLT_MAX);
	transformView->scale = max(transformView->scale, float3(0.0001f));

	if (ImGui::BeginPopupContextItem("scale"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->scale = float3(1.0f);
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isBaked)
			ImGui::Text("Disabled due to BakedTransformComponent!");
		auto scale = extractScale(transformView->calcModel());
		ImGui::Text("Scale of the entity\nGlobal: %.3f, %.3f, %.3f", scale.x, scale.y, scale.z);
		ImGui::EndTooltip();
	}

	if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f))
	{
		auto difference = newEulerAngles - oldEulerAngles;
		transformView->rotation *= quat(radians(difference));
		transformView->rotation = normalize(transformView->rotation);
		oldEulerAngles = newEulerAngles;
	}
	if (ImGui::BeginPopupContextItem("rotation"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->rotation = quat::identity;
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isBaked)
			ImGui::Text("Disabled due to BakedTransformComponent!");
		auto rotation = radians(newEulerAngles);
		auto global = degrees(extractQuat(extractRotation(transformView->calcModel())).toEulerAngles());
		ImGui::Text("Rotation in degrees\nRadians: %.3f, %.3f, %.3f\nGlobal: %.1f, %.1f, %.1f",
			rotation.x, rotation.y, rotation.z, global.x, global.y, global.z);
		ImGui::EndTooltip();
	}

	ImGui::EndDisabled();

	ImGui::InputText("Debug Name", &transformView->debugName);
	if (ImGui::BeginPopupContextItem("debugName"))
	{
		if (ImGui::MenuItem("Copy Name"))
			ImGui::SetClipboardText(transformView->debugName.c_str());
		if (ImGui::MenuItem("Paste Name"))
			transformView->debugName = ImGui::GetClipboardText();
		if (ImGui::MenuItem("Clear Name"))
			transformView->debugName = "";
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Entity debug only name");
		ImGui::EndTooltip();
	}
}
#endif