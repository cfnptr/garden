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

#include "garden/editor/system/ui/transform.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/transform.hpp"
#include "garden/system/transform.hpp"
#include "math/angles.hpp"

#include "imgui_internal.h"

using namespace garden;

//**********************************************************************************************************************
UiTransformEditorSystem::UiTransformEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", UiTransformEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", UiTransformEditorSystem::deinit);
}
UiTransformEditorSystem::~UiTransformEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", UiTransformEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", UiTransformEditorSystem::deinit);
	}

	unsetSingleton();
}

void UiTransformEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<UiTransformComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void UiTransformEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<UiTransformComponent>();
}

void UiTransformEditorSystem::onEntityDestroy(ID<Entity> entity)
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	if (editorSystem->selectedEntity == entity)
		editorSystem->selectedEntity = {};

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
void UiTransformEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto manager = Manager::Instance::get();
	if (entity != selectedEntity)
	{
		if (entity)
		{
			auto uiTransformView = manager->get<UiTransformComponent>(entity);
			oldRotation = uiTransformView->rotation;
			oldEulerAngles = newEulerAngles = degrees(oldRotation.extractEulerAngles());
		}

		selectedEntity = entity;
	}
	else
	{
		if (entity)
		{
			auto uiTransformView = manager->get<UiTransformComponent>(entity);
			if (oldRotation != uiTransformView->rotation)
			{
				oldRotation = uiTransformView->rotation;
				oldEulerAngles = newEulerAngles = degrees(oldRotation.extractEulerAngles());
			}
		}
	}

	if (!isOpened)
		return;

	auto uiTransformView = manager->get<UiTransformComponent>(entity);
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (transformView)
	{
		auto isSelfActive = transformView->isSelfActive();
		if (ImGui::Checkbox("Active", &isSelfActive))
			transformView->setActive(isSelfActive);
	}

	auto isStatic = manager->has<StaticTransformComponent>(entity);
	ImGui::BeginDisabled(isStatic);

	ImGui::Combo("Anchor", uiTransformView->anchor, uiAnchorNames, (int)UiAnchor::Count);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("UI element position relative point on the screen.");
		ImGui::EndTooltip();
	}

	ImGui::DragFloat3("Position", &uiTransformView->position, 1.0f);
	if (ImGui::BeginPopupContextItem("position"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiTransformView->position = float3::zero;
		ImGui::EndPopup();
	}
	if (isStatic && ImGui::BeginItemTooltip())
	{
		ImGui::Text("Disabled due to StaticTransformComponent!");
		ImGui::EndTooltip();
	}

	ImGui::DragFloat3("Scale", &uiTransformView->scale, 1.0f, 0.0001f, FLT_MAX);
	if (ImGui::BeginPopupContextItem("scale"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiTransformView->scale = float3::one;
		ImGui::EndPopup();
	}
	if (isStatic && ImGui::BeginItemTooltip())
	{
		ImGui::Text("Disabled due to StaticTransformComponent!");
		ImGui::EndTooltip();
	}

	if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f, 0.0f, 0.0f, "%.3f°"))
	{
		auto difference = newEulerAngles - oldEulerAngles;
		uiTransformView->rotation *= fromEulerAngles(radians(newEulerAngles - oldEulerAngles));
		uiTransformView->rotation = normalize(uiTransformView->rotation);
		oldEulerAngles = newEulerAngles;
	}
	if (ImGui::BeginPopupContextItem("rotation"))
	{
		if (ImGui::MenuItem("Reset Default"))
			uiTransformView->rotation = quat::identity;
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isStatic)
			ImGui::Text("Disabled due to StaticTransformComponent!");
		auto rotation = radians(newEulerAngles);
		ImGui::Text("Rotation in degrees.\nRadians: %.3f, %.3f, %.3f",
			rotation.getX(), rotation.getY(), rotation.getZ());
		ImGui::EndTooltip();
	}

	ImGui::EndDisabled();

	if (transformView)
	{
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
}
#endif