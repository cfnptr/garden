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

#include "garden/editor/system/render/mesh-selector.hpp"
#include "garden/system/render/deferred.hpp"

#if GARDEN_EDITOR
#include "garden/system/settings.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/render/mesh.hpp"
#include "math/matrix/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
MeshSelectorEditorSystem::MeshSelectorEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", MeshSelectorEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", MeshSelectorEditorSystem::deinit);
}
MeshSelectorEditorSystem::~MeshSelectorEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", MeshSelectorEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", MeshSelectorEditorSystem::deinit);
	}
}

void MeshSelectorEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("MetaLdrRender", MeshSelectorEditorSystem::metaLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", MeshSelectorEditorSystem::editorSettings);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getColor("meshSelector.aabbColor", aabbColor);
}
void MeshSelectorEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("MetaLdrRender", MeshSelectorEditorSystem::metaLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", MeshSelectorEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void MeshSelectorEditorSystem::metaLdrRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!isEnabled || !graphicsSystem->camera)
		return;

	auto manager = Manager::Instance::get();
	auto inputSystem = InputSystem::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	auto editorSystem = EditorRenderSystem::Instance::get();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = cameraConstants.cameraPos;
	auto selectedEntity = editorSystem->selectedEntity;

	auto updateSelector = !ImGui::GetIO().WantCaptureMouse && !lastDragging &&
		inputSystem->getCursorMode() == CursorMode::Normal && inputSystem->isMouseReleased(MouseButton::Left);
	lastDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);

	if (updateSelector && !isSkipped)
	{
		const auto& systems = manager->getSystems();
		auto windowSize = inputSystem->getWindowSize();
		auto cursorPosition = inputSystem->getCursorPosition();
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalOrigin = cameraConstants.invViewProj * f32x4(ndcPosition.x, ndcPosition.y, 1.0f, 1.0f);
		auto globalDirection = cameraConstants.invViewProj * f32x4(ndcPosition.x, ndcPosition.y, 0.0001f, 1.0f);
		globalOrigin /= globalOrigin.getW(); globalDirection /= globalDirection.getW();
		globalDirection -= globalOrigin;

		auto newDistSq = FLT_MAX;
		ID<Entity> newSelected; Aabb newAabb;

		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (!meshSystem)
				continue;
			
			const auto& componentPool = meshSystem->getMeshComponentPool();
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (const uint8*)componentPool.getData();
			auto componentOccupancy = componentPool.getOccupancy();

			for (uint32 i = 0; i < componentOccupancy; i++)
			{
				auto meshRenderView = (const MeshRenderComponent*)(componentData + i * componentSize);
				if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
					continue;

				auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
				if (!transformView->isActive())
					continue;

				auto model = transformView ? transformView->calcModel(cameraPosition) : f32x4x4::identity;
				auto modelInverse = inverse4x4(model);
				auto ray = Ray(modelInverse * f32x4(globalOrigin, 1.0f), multiply3x3(modelInverse, globalDirection));
				auto points = raycast2(meshRenderView->aabb, ray);
				if (points.x < 0.0f || !isAabbIntersected(points))
					continue;
			
				auto distSq = distanceSq3(globalOrigin, getTranslation(model));
				if (distSq < newDistSq && meshRenderView->getEntity() != selectedEntity)
				{
					newSelected = meshRenderView->getEntity();
					newDistSq = distSq;
					newAabb = meshRenderView->aabb;
				}
			}
		}

		if (newSelected)
			editorSystem->selectedEntity = newSelected;
		else
			editorSystem->selectedEntity = {};
	}

	if (isSkipped)
		isSkipped = false;

	if (selectedEntity)
	{
		const auto& systems = manager->getSystems();
		Aabb selectedEntityAabb = Aabb::inf;

		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (!meshSystem)
				continue;

			const auto& componentPool = meshSystem->getMeshComponentPool();
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (const uint8*)componentPool.getData();
			auto componentOccupancy = componentPool.getOccupancy();

			for (uint32 i = 0; i < componentOccupancy; i++)
			{
				auto meshRenderView = (const MeshRenderComponent*)(componentData + i * componentSize);
				if (!meshRenderView->isEnabled || selectedEntity != meshRenderView->getEntity())
					continue;
				selectedEntityAabb.shrink(meshRenderView->aabb);
				break;
			}
		}

		auto transformView = transformSystem->tryGetComponent(selectedEntity);
		if (transformView && selectedEntityAabb != Aabb::inf)
		{
			auto model = transformView->calcModel(cameraPosition);
			auto mvp = cameraConstants.viewProj * model * translate(
				selectedEntityAabb.getPosition()) * scale(selectedEntityAabb.getSize());

			SET_GPU_DEBUG_LABEL("Selected Mesh AABB", Color::transparent);
			graphicsSystem->drawAabb(mvp, (f32x4)aabbColor);
		}
	}
}

//**********************************************************************************************************************
void MeshSelectorEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Mesh Selector"))
	{
		auto settingsSystem = SettingsSystem::Instance::tryGet();
		ImGui::Indent();
		ImGui::Checkbox("Enabled", &isEnabled);

		if (ImGui::ColorEdit4("AABB Color", &aabbColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshSelector.aabbColor", aabbColor);
		}

		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif