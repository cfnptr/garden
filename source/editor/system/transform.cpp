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

#include "garden/editor/system/transform.hpp"

#if GARDEN_EDITOR
#include "garden/system/ui/transform.hpp"
#include "garden/system/transform.hpp"
#include "math/matrix/transform.hpp"
#include "math/angles.hpp"

#include "imgui_internal.h"

using namespace garden;

//**********************************************************************************************************************
TransformEditorSystem::TransformEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", TransformEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", TransformEditorSystem::deinit);
}
TransformEditorSystem::~TransformEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", TransformEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", TransformEditorSystem::deinit);
	}

	unsetSingleton();
}

void TransformEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<TransformComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void TransformEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<TransformComponent>();
}

//**********************************************************************************************************************
void TransformEditorSystem::onEntityDestroy(ID<Entity> entity)
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
void TransformEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto manager = Manager::Instance::get();
	if (ImGui::BeginItemTooltip())
	{
		auto transformView = manager->get<TransformComponent>(entity);
		ImGui::Text("Active (all): %s", transformView->isActive() ? "true" : "false");
		if (transformView->getParent())
		{
			auto parentView = manager->get<TransformComponent>(transformView->getParent());
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
			auto transformView = manager->get<TransformComponent>(entity);
			oldRotation = transformView->getRotation();
			oldEulerAngles = newEulerAngles = degrees(oldRotation.extractEulerAngles());
		}

		selectedEntity = entity;
	}
	else
	{
		if (entity)
		{
			auto transformView = manager->get<TransformComponent>(entity);
			if (oldRotation != transformView->getRotation())
			{
				oldRotation = transformView->getRotation();
				oldEulerAngles = newEulerAngles = degrees(oldRotation.extractEulerAngles());
			}
		}
	}

	if (!isOpened)
		return;

	if (manager->has<UiTransformComponent>(entity))
	{
		ImGui::Text("Controlled by the UiTransformComponent!");
		return;
	}

	auto transformView = manager->get<TransformComponent>(entity);
	auto isSelfActive = transformView->isSelfActive();
	if (ImGui::Checkbox("Active", &isSelfActive))
		transformView->setActive(isSelfActive);

	auto isStatic = manager->has<StaticTransformComponent>(entity);
	ImGui::BeginDisabled(isStatic);

	auto f32x4Value = transformView->getPosition();
	if (ImGui::DragFloat3("Position", &f32x4Value, 0.01f))
		transformView->setPosition(f32x4Value);
	if (ImGui::BeginPopupContextItem("position"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->setPosition(f32x4::zero);
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isStatic)
			ImGui::Text("Disabled due to StaticTransformComponent!");
		auto translation = getTranslation(transformView->calcModel());
		ImGui::Text("Position of the entity.\nGlobal: %.1f, %.1f, %.1f",
			translation.getX(), translation.getY(), translation.getZ());
		ImGui::EndTooltip();
	}

	f32x4Value = transformView->getScale();
	if (ImGui::DragFloat3("Scale", &f32x4Value, 0.01f, 0.0f, FLT_MAX))
		transformView->setScale(max(f32x4Value, f32x4(0.0001f)));

	if (ImGui::BeginPopupContextItem("scale"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->setScale(f32x4::one);
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isStatic)
			ImGui::Text("Disabled due to StaticTransformComponent!");
		auto scale = extractScale(transformView->calcModel());
		ImGui::Text("Scale of the entity.\nGlobal: %.3f, %.3f, %.3f", 
			scale.getX(), scale.getY(), scale.getZ());
		ImGui::EndTooltip();
	}

	if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f, 0.0f, 0.0f, "%.3f°"))
	{
		transformView->rotate(fromEulerAngles(radians(newEulerAngles - oldEulerAngles)));
		transformView->setRotation(normalize(transformView->getRotation()));
		oldEulerAngles = newEulerAngles;
	}
	if (ImGui::BeginPopupContextItem("rotation"))
	{
		if (ImGui::MenuItem("Reset Default"))
			transformView->setRotation(quat::identity);
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		if (isStatic)
			ImGui::Text("Disabled due to StaticTransformComponent!");
		auto quat = extractQuat(extractRotation(transformView->calcModel()));
		auto global = degrees(quat.extractEulerAngles());
		auto rotation = radians(newEulerAngles);
		ImGui::Text("Rotation in degrees.\nGlobal: %.1f, %.1f, %.1f\n"
			"Radians: %.3f, %.3f, %.3f\nQuat: %.3f, %.3f, %.3f %.3f",
			global.getX(), global.getY(), global.getZ(), 
			rotation.getX(), rotation.getY(), rotation.getZ(),
			quat.getX(), quat.getY(), quat.getZ(), quat.getW());
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