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

#include "garden/editor/system/render/mesh-selector.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/gizmos.hpp" // TODO: remove?

using namespace garden;

//**********************************************************************************************************************
MeshSelectorEditorSystem::MeshSelectorEditorSystem(Manager* manager,
	MeshRenderSystem* system) : EditorSystem(manager, system)
{
	SUBSCRIBE_TO_EVENT("EditorRender", MeshSelectorEditorSystem::editorRender);
}
MeshSelectorEditorSystem::~MeshSelectorEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("EditorRender", MeshSelectorEditorSystem::editorRender);
}

//**********************************************************************************************************************
void MeshSelectorEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto manager = getManager();
	auto inputSystem = InputSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto selectedEntity = editorSystem->selectedEntity;

	auto updateSelector = true;
	if (!ImGui::GetIO().WantCaptureMouse && inputSystem->getCursorMode() == CursorMode::Default &&
		inputSystem->isMouseClicked(MouseButton::Left))
	{
		updateSelector = false;
	}

	if (updateSelector && !isSkipped)
	{
		auto windowSize = graphicsSystem->getWindowSize();
		auto cursorPosition = inputSystem->getCursorPosition();
		auto uvPosition = (cursorPosition + 0.5f) / windowSize;
		auto globalDirection = (float3)(cameraConstants.viewProjInv *
			float4(uvPosition * 2.0f - 1.0f, 0.0f, 1.0f));
		auto& transformComponents = TransformSystem::getInstance()->getComponents();
		auto& systems = manager->getSystems();

		float newDistance = FLT_MAX;
		ID<Entity> newSelected; Aabb newAabb;
		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (!meshSystem)
				continue;
			
			auto& componentPool = meshSystem->getMeshComponentPool();
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (const uint8*)componentPool.getData();
			auto componentOccupancy = componentPool.getOccupancy();

			for (uint32 i = 0; i < componentOccupancy; i++)
			{
				auto meshRender = (const MeshRenderComponent*)(
					componentData + i * componentSize);
				auto entity = meshRender->getEntity();
				if (!entity || !meshRender->isEnabled)
					continue;

				auto transform = transformComponents.get(meshRender->getTransform());
				auto model = transform->calcModel();
				setTranslation(model, getTranslation(model) - cameraPosition);
				auto modelInverse = inverse(model);
				auto localOrigin = modelInverse * float4(0.0f, 0.0f, 0.0f, 1.0f);
				auto localDirection = float3x3(modelInverse) * globalDirection;
				auto ray = Ray((float3)localOrigin, (float3)localDirection);
				auto points = raycast2(meshRender->aabb, ray);
				if (points.x < 0.0f || !isIntersected(points))
					continue;
			
				if (points.x < newDistance && entity != selectedEntity)
				{
					newSelected = entity;
					newDistance = points.x;
					newAabb = meshRender->aabb;
				}
			}
		}

		if (newSelected)
		{
			editorSystem->selectedEntity = newSelected;
			editorSystem->selectedEntityAabb = newAabb;
		}
		else
		{
			editorSystem->selectedEntity = {};
		}
	}

	if (isSkipped)
		isSkipped = false;

	if (selectedEntity && editorSystem->selectedEntityAabb != Aabb())
	{
		auto transform = manager->tryGet<TransformComponent>(selectedEntity);
		if (transform)
		{
			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			auto model = transform->calcModel();
			setTranslation(model, getTranslation(model) - cameraPosition);

			SET_GPU_DEBUG_LABEL("Selected Mesh AABB", Color::transparent);
			framebufferView->beginRenderPass(float4(0.0f));
			auto mvp = cameraConstants.viewProj * model *
				translate(editorSystem->selectedEntityAabb.getPosition()) *
				scale(editorSystem->selectedEntityAabb.getSize());
			graphicsSystem->drawAabb(mvp);
			framebufferView->endRenderPass();
		}
	}
}
#endif