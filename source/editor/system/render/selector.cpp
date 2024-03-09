//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/editor/system/render/selector.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/deferred.hpp"
#include "garden/editor/system/render/gizmos.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
SelectorEditor::SelectorEditor(MeshRenderSystem* system)
{
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void SelectorEditor::preSwapchainRender()
{
	auto graphicsSystem = system->getGraphicsSystem();
	if (!graphicsSystem->camera)
		return;

	auto manager = system->getManager();
	auto editorSystem = EditorRenderSystem::getInstance();
	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto selectedEntity = editorSystem->selectedEntity;
	auto gizmosEditor = (GizmosEditor*)system->gizmosEditor;

	auto updateSelector = true;
	if (!ImGui::GetIO().WantCaptureMouse &&
		graphicsSystem->getCursorMode() == CursorMode::Default &&
		graphicsSystem->isMouseButtonPressed(MouseButton::Left) &&
		!gizmosEditor->lastLmbState)
	{
		lastLmbState = true;
		updateSelector = false;
	}
	else
	{
		if (!lastLmbState) updateSelector = false;
		lastLmbState = false;
	}

	if (updateSelector && !isSkipped)
	{
		auto windowSize = graphicsSystem->getWindowSize();
		auto cursorPosition = graphicsSystem->getCursorPosition();
		auto uvPosition = (cursorPosition + 0.5f) / windowSize;
		auto globalDirection = (float3)(cameraConstants.viewProjInv *
			float4(uvPosition * 2.0f - 1.0f, 0.0f, 1.0f));
		auto& subsystems = manager->getSubsystems<MeshRenderSystem>();
		auto& transformComponents = TransformSystem::getInstance()->getComponents();

		float newDistance = FLT_MAX;
		ID<Entity> newSelected; Aabb newAabb;
		for (auto subsystem : subsystems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(subsystem.system);
			GARDEN_ASSERT(meshSystem);
			
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
			auto deferredSystem = manager->get<DeferredRenderSystem>();
			auto framebufferView = graphicsSystem->get(
				deferredSystem->getEditorFramebuffer());
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