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

#include "garden/editor/system/camera.hpp"

#if GARDEN_EDITOR
#include "garden/system/camera.hpp"

using namespace garden;

//**********************************************************************************************************************
CameraEditorSystem::CameraEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", CameraEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", CameraEditorSystem::deinit);
}
CameraEditorSystem::~CameraEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", CameraEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", CameraEditorSystem::deinit);
	}
}

void CameraEditorSystem::init()
{
	GARDEN_ASSERT(Manager::getInstance()->has<EditorRenderSystem>());
	EditorRenderSystem::getInstance()->registerEntityInspector<CameraComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void CameraEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<CameraComponent>();
}

//**********************************************************************************************************************
void CameraEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto cameraComponent = Manager::getInstance()->get<CameraComponent>(entity);
		ImGui::Text("Projection: ", cameraComponent->type == 
			ProjectionType::Perspective ? "perspective" : "orthographic");
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto cameraComponent = Manager::getInstance()->get<CameraComponent>(entity);
	if (cameraComponent->type == ProjectionType::Perspective)
	{
		float fov = degrees(cameraComponent->p.perspective.fieldOfView);
		if (ImGui::DragFloat("Filed Of View", &fov, 0.1f, 0.0f, FLT_MAX))
			cameraComponent->p.perspective.fieldOfView = radians(fov);
		if (ImGui::BeginPopupContextItem("fieldOfView"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.perspective.fieldOfView = radians(defaultFieldOfView);
			ImGui::EndPopup();
		}

		ImGui::DragFloat("Aspect Ratio", &cameraComponent->p.perspective.aspectRatio, 0.01f, 0.0f, FLT_MAX);
		if (ImGui::BeginPopupContextItem("aspectRatio"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.perspective.aspectRatio = defaultAspectRatio;
			ImGui::EndPopup();
		}

		ImGui::DragFloat("Near Plane", &cameraComponent->p.perspective.nearPlane, 0.01f, 0.0f, FLT_MAX);
		if (ImGui::BeginPopupContextItem("nearPlane"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.perspective.nearPlane = defaultHmdDepth;
			ImGui::EndPopup();
		}
	}
	else if (cameraComponent->type == ProjectionType::Orthographic)
	{
		ImGui::DragFloat2("Width", (float*)&cameraComponent->p.orthographic.width, 0.1f);
		if (ImGui::BeginPopupContextItem("width"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.orthographic.width = float2(-1.0f, 1.0f);
			ImGui::EndPopup();
		}

		ImGui::DragFloat2("Height", (float*)&cameraComponent->p.orthographic.height, 0.1f);
		if (ImGui::BeginPopupContextItem("height"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.orthographic.height = float2(-1.0f, 1.0f);
			ImGui::EndPopup();
		}

		ImGui::DragFloat2("Depth", (float*)&cameraComponent->p.orthographic.depth, 0.1f);
		if (ImGui::BeginPopupContextItem("depth"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cameraComponent->p.orthographic.depth = float2(-1.0f, 1.0f);
			ImGui::EndPopup();
		}
	}
	else abort();

	const auto types = "Perspective\0Orthographic\00";
	if (ImGui::Combo("Type", &cameraComponent->type, types))
	{
		if (cameraComponent->type == ProjectionType::Perspective)
			cameraComponent->p.perspective = PerspectiveProjection();
		else if (cameraComponent->type == ProjectionType::Orthographic)
			cameraComponent->p.orthographic = OrthographicProjection();
		else abort();
	}
}
#endif