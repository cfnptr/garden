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
#include "garden/system/settings.hpp"
#include "garden/system/render/mesh.hpp"
#include "math/matrix/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
MeshSelectorEditorSystem::MeshSelectorEditorSystem()
{
	SUBSCRIBE_TO_EVENT("Init", MeshSelectorEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", MeshSelectorEditorSystem::deinit);
}
MeshSelectorEditorSystem::~MeshSelectorEditorSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", MeshSelectorEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", MeshSelectorEditorSystem::deinit);
	}
}

void MeshSelectorEditorSystem::init()
{
	SUBSCRIBE_TO_EVENT("EditorRender", MeshSelectorEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorSettings", MeshSelectorEditorSystem::editorSettings);

	auto settingsSystem = Manager::get()->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getColor("meshSelectorColor", aabbColor);
}
void MeshSelectorEditorSystem::deinit()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", MeshSelectorEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorSettings", MeshSelectorEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
void MeshSelectorEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::get();
	if (!isEnabled || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto manager = Manager::get();
	auto inputSystem = InputSystem::get();
	auto transformSystem = TransformSystem::get();
	auto editorSystem = EditorRenderSystem::get();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto selectedEntity = editorSystem->selectedEntity;

	auto updateSelector = !ImGui::GetIO().WantCaptureMouse && !lastDragging &&
		inputSystem->getCursorMode() == CursorMode::Default && inputSystem->isMouseReleased(MouseButton::Left);
	lastDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);

	if (updateSelector && !isSkipped)
	{
		const auto& systems = manager->getSystems();
		auto windowSize = graphicsSystem->getWindowSize();
		auto cursorPosition = inputSystem->getCursorPosition();
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalOrigin = cameraConstants.viewProjInv * float4(ndcPosition, 1.0f, 1.0f);
		auto globalDirection = cameraConstants.viewProjInv * float4(ndcPosition, 0.0001f, 1.0f);
		globalOrigin = float4((float3)globalOrigin / globalOrigin.w, globalOrigin.w);
		globalDirection = float4((float3)globalDirection / globalDirection.w - (float3)globalOrigin, globalDirection.w);

		auto newDist2 = FLT_MAX;
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

				auto transformView = transformSystem->tryGet(meshRenderView->getEntity());
				if (!transformView->isActiveWithAncestors())
					continue;

				float4x4 model;
				if (transformView)
					model = transformView->calcModel(cameraPosition);
				else
					model = float4x4::identity;

				auto modelInverse = inverse(model);
				auto localOrigin = modelInverse * float4((float3)globalOrigin, 1.0f);
				auto localDirection = (float3x3)modelInverse * (float3)globalDirection;
				auto ray = Ray((float3)localOrigin, (float3)localDirection);
				auto points = raycast2(meshRenderView->aabb, ray);
				if (points.x < 0.0f || !isAabbIntersected(points))
					continue;
			
				auto dist2 = distance2((float3)globalOrigin, getTranslation(model));
				if (dist2 < newDist2 && meshRenderView->getEntity() != selectedEntity)
				{
					newSelected = meshRenderView->getEntity();
					newDist2 = dist2;
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
		Aabb selectedEntityAabb = {};

		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (!meshSystem)
				continue;

			const auto& componentPool = meshSystem->getMeshComponentPool();
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (const uint8*)componentPool.getData();
			auto componentOccupancy = componentPool.getOccupancy();

			auto breakOut = false;
			for (uint32 i = 0; i < componentOccupancy; i++)
			{
				auto meshRenderView = (const MeshRenderComponent*)(componentData + i * componentSize);
				if (selectedEntity != meshRenderView->getEntity())
					continue;
				selectedEntityAabb = meshRenderView->aabb;
				breakOut = true;
				break;
			}

			if (breakOut)
				break;
		}

		auto transformView = transformSystem->tryGet(selectedEntity);
		if (transformView && selectedEntityAabb != Aabb())
		{
			auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
			auto model = transformView->calcModel(cameraPosition);
			auto mvp = cameraConstants.viewProj * model * translate(
				selectedEntityAabb.getPosition()) * scale(selectedEntityAabb.getSize());

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Selected Mesh AABB", Color::transparent);
				framebufferView->beginRenderPass(float4(0.0f));
				graphicsSystem->drawAabb(mvp, (float4)aabbColor);
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
}

//**********************************************************************************************************************
void MeshSelectorEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Mesh Selector"))
	{
		auto settingsSystem = Manager::get()->tryGet<SettingsSystem>();
		ImGui::Indent();
		ImGui::Checkbox("Enabled", &isEnabled);

		if (ImGui::ColorEdit4("AABB Color", &aabbColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshSelectorColor", aabbColor);
		}

		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif