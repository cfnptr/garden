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
#include "garden/system/render/editor.hpp"

using namespace garden;

CameraEditorSystem::CameraEditorSystem(Manager* manager, CameraSystem* system) : EditorSystem(manager, system)
{
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->registerEntityInspector<CameraComponent>([this](ID<Entity> entity)
	{
		onEntityInspector(entity);
	});
}

//**********************************************************************************************************************
void CameraEditorSystem::onEntityInspector(ID<Entity> entity)
{
	ImGui::PushID("CameraComponent");
	if (ImGui::CollapsingHeader("Camera"))
	{
		auto cameraComponent = getManager()->get<CameraComponent>(entity);
		if (cameraComponent->type == CameraComponent::Type::Perspective)
		{
			ImGui::Text("Type: Perspective");
			float fov = degrees(cameraComponent->p.perspective.fieldOfView);
			if (ImGui::DragFloat("Filed Of View", &fov, 0.1f, 0.0f, FLT_MAX))
				cameraComponent->p.perspective.fieldOfView = radians(fov);
			ImGui::DragFloat("Aspect Ratio", &cameraComponent->p.perspective.aspectRatio, 0.01f, 0.0f, FLT_MAX);
			ImGui::DragFloat("Near Plane", &cameraComponent->p.perspective.nearPlane, 0.01f, 0.0f, FLT_MAX);
		}
		else if (cameraComponent->type == CameraComponent::Type::Orthographic)
		{
			ImGui::Text("Type: Orthographic");
			ImGui::DragFloat2("Width", (float*)&cameraComponent->p.orthographic.width, 0.1f);
			ImGui::DragFloat2("Height", (float*)&cameraComponent->p.orthographic.height, 0.1f);
			ImGui::DragFloat2("Depth", (float*)&cameraComponent->p.orthographic.depth, 0.1f);
		}
		else abort();
		ImGui::Spacing();
	}
	ImGui::PopID();
}
#endif