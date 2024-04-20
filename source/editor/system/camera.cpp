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
using namespace garden;

//**********************************************************************************************************************
CameraEditorSystem::CameraEditorSystem(Manager* manager, CameraSystem* system) : EditorSystem(manager, system)
{
	EditorRenderSystem::getInstance()->registerEntityInspector<CameraComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
CameraEditorSystem::~CameraEditorSystem()
{
	if (getManager()->isRunning())
		EditorRenderSystem::getInstance()->unregisterEntityInspector<CameraComponent>();
}

//**********************************************************************************************************************
void CameraEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto cameraComponent = getManager()->get<CameraComponent>(entity);
	if (cameraComponent->type == ProjectionType::Perspective)
	{
		float fov = degrees(cameraComponent->p.perspective.fieldOfView);
		if (ImGui::DragFloat("Filed Of View", &fov, 0.1f, 0.0f, FLT_MAX))
			cameraComponent->p.perspective.fieldOfView = radians(fov);
		ImGui::DragFloat("Aspect Ratio", &cameraComponent->p.perspective.aspectRatio, 0.01f, 0.0f, FLT_MAX);
		ImGui::DragFloat("Near Plane", &cameraComponent->p.perspective.nearPlane, 0.01f, 0.0f, FLT_MAX);
	}
	else if (cameraComponent->type == ProjectionType::Orthographic)
	{
		ImGui::DragFloat2("Width", (float*)&cameraComponent->p.orthographic.width, 0.1f);
		ImGui::DragFloat2("Height", (float*)&cameraComponent->p.orthographic.height, 0.1f);
		ImGui::DragFloat2("Depth", (float*)&cameraComponent->p.orthographic.depth, 0.1f);
	}
	else abort();

	auto types = "Perspective\0Orthographic\00";
	if (ImGui::Combo("Type", cameraComponent->type, types))
	{
		if (cameraComponent->type == ProjectionType::Perspective)
			cameraComponent->p.perspective = PerspectiveProjection();
		else if (cameraComponent->type == ProjectionType::Orthographic)
			cameraComponent->p.orthographic = OrthographicProjection();
		else abort();
	}
}
#endif