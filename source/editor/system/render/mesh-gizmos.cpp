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

#include "garden/editor/system/render/mesh-gizmos.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/mesh-selector.hpp"
#include "garden/system/render/mesh.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "math/angles.hpp"

using namespace garden;

// TODO: change winding order, and use better quad layout

static const array<float3, 18> fullArrowVert =
{
	// -X side
	float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.0f,  0.5f,  0.0f),
	// -Z side
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.0f,  0.5f, -0.0f),
	float3( 0.5f, -0.5f, -0.5f),
	// -Y side
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	// +X side
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.0f,  0.5f, -0.0f),
	// +Z side
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.0f,  0.5f,  0.0f),
};

namespace
{
	struct PushConstants final
	{
		float4x4 mvp;
		float4 color;
		float renderScale;
	};

	struct GizmosMesh final
	{
		float4x4 model = float4x4(0.0f);
		Color color = Color::black;
		ID<Buffer> vertexBuffer = {};
		float distance = 0.0f;
	};
}

//**********************************************************************************************************************
MeshGizmosEditorSystem::MeshGizmosEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", MeshGizmosEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", MeshGizmosEditorSystem::deinit);
	
}
MeshGizmosEditorSystem::~MeshGizmosEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", MeshGizmosEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", MeshGizmosEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());
	
	SUBSCRIBE_TO_EVENT("EditorRender", MeshGizmosEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorSettings", MeshGizmosEditorSystem::editorSettings);

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto resourceSystem = ResourceSystem::getInstance();
	auto swapchainFramebuffer = graphicsSystem->getSwapchainFramebuffer();

	frontGizmosPipeline = resourceSystem->loadGraphicsPipeline("editor/gizmos-front", swapchainFramebuffer);
	backGizmosPipeline = resourceSystem->loadGraphicsPipeline("editor/gizmos-back", swapchainFramebuffer);

	fullArrowVertices = graphicsSystem->createBuffer(Buffer::Bind::Vertex | Buffer::Bind::TransferDst,
		Buffer::Access::None, fullArrowVert, 0, 0, Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, fullArrowVertices, "buffer.vertex.gizmos.arrow");

	auto settingsSystem = manager->tryGet<SettingsSystem>();
	if (settingsSystem)
	{
		settingsSystem->getColor("meshGizmosColor", handleColor);
		settingsSystem->getColor("meshGizmosColorX", axisColorX);
		settingsSystem->getColor("meshGizmosColorY", axisColorY);
		settingsSystem->getColor("meshGizmosColorZ", axisColorZ);
		settingsSystem->getFloat("meshGizmosFactor", highlighFactor);
	}
}
void MeshGizmosEditorSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(fullArrowVertices);
		graphicsSystem->destroy(backGizmosPipeline);
		graphicsSystem->destroy(frontGizmosPipeline);

		UNSUBSCRIBE_FROM_EVENT("EditorRender", MeshGizmosEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorSettings", MeshGizmosEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
static void addArrowMeshes(vector<GizmosMesh>& gizmosMeshes, const float4x4& model, ID<Buffer> fullCube,
	ID<Buffer> fullArrow, Color handleColor, Color axisColorX, Color axisColorY, Color axisColorZ)
{
	GizmosMesh gizmosMesh;
	gizmosMesh.vertexBuffer = fullCube;
	gizmosMesh.model = model * scale(float3(0.1f, 0.1f, 0.1f));
	gizmosMesh.color = handleColor;
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = fullArrow;
	gizmosMesh.model = model * translate(float3(0.9f, 0.0f, 0.0f)) *
		rotate(quat(radians(90.0f), float3::back)) * scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorX;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.9f, 0.0f)) *
		scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorY;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.0f, 0.9f)) *
		rotate(quat(radians(90.0f), float3::right)) * scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorZ;
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = fullCube;
	gizmosMesh.model = model * translate(float3(0.425f, 0.0f, 0.0f)) * scale(float3(0.75f, 0.05f, 0.05f));
	gizmosMesh.color = axisColorX;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.425f, 0.0f)) * scale(float3(0.05f, 0.75f, 0.05f));
	gizmosMesh.color = axisColorY;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.0f, 0.425f)) * scale(float3(0.05f, 0.05f, 0.75f));
	gizmosMesh.color = axisColorZ;
	gizmosMeshes.push_back(gizmosMesh);
}

//**********************************************************************************************************************
static void renderGizmosArrows(vector<GizmosMesh>& gizmosMeshes,
	View<GraphicsPipeline> pipelineView, const float4x4& viewProj, bool sortAscend)
{
	std::function<bool(const GizmosMesh& a, const GizmosMesh& b)> ascending =
		[](const GizmosMesh& a, const GizmosMesh& b) { return a.distance < b.distance; };
	std::function<bool(const GizmosMesh& a, const GizmosMesh& b)> descending =
		[](const GizmosMesh& a, const GizmosMesh& b) { return a.distance > b.distance; };
	sort(gizmosMeshes.begin(), gizmosMeshes.end(), sortAscend ? ascending : descending);

	pipelineView->bind();
	pipelineView->setViewportScissor();

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->renderScale = 0.25f / graphicsSystem->getRenderScale();
	
	for (const auto& mesh : gizmosMeshes)
	{
		auto bufferView = graphicsSystem->get(mesh.vertexBuffer);
		pushConstants->mvp = viewProj * mesh.model;
		pushConstants->color = (float4)mesh.color;
		pipelineView->pushConstants();
		pipelineView->draw(mesh.vertexBuffer, (uint32)(bufferView->getBinarySize() / sizeof(float3)));
	}
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto selectedEntity = EditorRenderSystem::getInstance()->selectedEntity;

	if (!isEnabled || !graphicsSystem->canRender() || !selectedEntity ||
		!graphicsSystem->camera || selectedEntity == graphicsSystem->camera)
	{
		return;
	}

	auto inputSystem = InputSystem::getInstance();
	if (!inputSystem->getMouseState(MouseButton::Left))
		dragMode = 0;
	
	auto manager = Manager::getInstance();
	auto transform = manager->tryGet<TransformComponent>(selectedEntity);
	auto frontPipelineView = graphicsSystem->get(frontGizmosPipeline);
	auto backPipelineView = graphicsSystem->get(backGizmosPipeline);
	auto fullCubeVertices = graphicsSystem->getFullCubeVertices();
	auto fullCubeView = graphicsSystem->get(fullCubeVertices);
	auto fullArrowView = graphicsSystem->get(fullArrowVertices);

	// TODO: check for selection freeze problem.

	if (!transform || transform->hasBakedWithDescendants() || !frontPipelineView->isReady() ||
		!backPipelineView->isReady() || !fullCubeView->isReady() || !fullArrowView->isReady())
	{
		return;
	}

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto model = transform->calcModel(cameraPosition);
	
	auto windowSize = graphicsSystem->getWindowSize();
	auto cursorPosition = inputSystem->getCursorPosition();
	auto rotation = (inputSystem->getKeyboardState(KeyboardButton::LeftShift) ||
		inputSystem->getKeyboardState(KeyboardButton::RightShift)) &&
		inputSystem->getCursorMode() == CursorMode::Default ?
		quat::identity : extractQuat(extractRotation(model));
	auto translation = getTranslation(model);

	auto modelScale = 0.25f;
	auto cameraComponent = manager->tryGet<CameraComponent>(graphicsSystem->camera);
	if (cameraComponent && cameraComponent->type == ProjectionType::Perspective)
		modelScale *= length(translation);
	else
		modelScale *= (cameraComponent->p.orthographic.height.y - cameraComponent->p.orthographic.height.x) * 0.5f;
	model = calcModel(translation, rotation, float3(modelScale));

	vector<GizmosMesh> gizmosMeshes;
	addArrowMeshes(gizmosMeshes, model, fullCubeVertices, fullArrowVertices,
		handleColor, axisColorX, axisColorY, axisColorZ);
	// TODO: scale and rotation gizmos.
	
	if (!ImGui::GetIO().WantCaptureMouse && inputSystem->getCursorMode() == CursorMode::Default)
	{
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalOrigin = cameraConstants.viewProjInv * float4(ndcPosition, 1.0f, 1.0f);
		auto globalDirection = cameraConstants.viewProjInv * float4(ndcPosition, 0.0001f, 1.0f);
		globalOrigin = float4((float3)globalOrigin / globalOrigin.w, globalOrigin.w);
		globalDirection = float4((float3)globalDirection / globalDirection.w - (float3)globalOrigin, globalDirection.w);
		
		float newDist2 = FLT_MAX; uint32 meshIndex = 0;
		for (uint32 i = 0; i < 4; i++)
		{
			auto modelInverse = inverse(gizmosMeshes[i].model);
			auto localOrigin = modelInverse * float4((float3)globalOrigin, 1.0f);
			auto localDirection = (float3x3)modelInverse * (float3)globalDirection;
			auto ray = Ray((float3)localOrigin, (float3)localDirection);
			if (!raycast(Aabb::two, ray))
				continue;

			auto dist2 = distance2((float3)globalOrigin, getTranslation(gizmosMeshes[i].model));
			if (dist2 < newDist2)
			{
				meshIndex = i;
				newDist2 = dist2;
			}
		}

		if (newDist2 != FLT_MAX)
		{
			auto& mesh = gizmosMeshes[meshIndex];
			mesh.model *= scale(float3(1.25f));
			mesh.color = Color((float4)mesh.color * highlighFactor);

			if (inputSystem->isMousePressed(MouseButton::Left))
				dragMode = meshIndex + 1;
		}
	}

	for (auto& mesh : gizmosMeshes)
		mesh.distance = length2(getTranslation(mesh.model));

	if (dragMode != 0)
	{
		auto cursorPosition = inputSystem->getCursorPosition();
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalLastPos = (float3)(cameraConstants.viewProjInv * float4(ndcPosition, 0.0f, 1.0f));
		cursorPosition += inputSystem->getCursorDelta();
		ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalNewPos = (float3)(cameraConstants.viewProjInv * float4(ndcPosition, 0.0f, 1.0f));

		if (dragMode != 1)
		{
			auto modelInv = (float3x3)inverse(calcModel(translation, rotation, float3(1.0f)));
			globalLastPos = modelInv * globalLastPos;
			globalNewPos = modelInv * globalNewPos;
		}

		auto cursorTrans = (globalNewPos - globalLastPos); 
		switch (dragMode)
		{
		case 2: cursorTrans.y = cursorTrans.z = 0.0f; break;
		case 3: cursorTrans.x = cursorTrans.z = 0.0f; break;
		case 4: cursorTrans.x = cursorTrans.y = 0.0f; break;
		}

		if (dragMode != 1 && !inputSystem->getKeyboardState(KeyboardButton::LeftShift) &&
			!inputSystem->getKeyboardState(KeyboardButton::RightShift))
		{
			cursorTrans = (float3x3)rotate(transform->rotation) * cursorTrans;
		}

		// TODO: Fix non-uniform transformation in perspective projection,
		//       take into account perspective distortion if possible.
		if (cameraComponent->type == ProjectionType::Perspective)
			cursorTrans *= length(translation);

		transform->position += cursorTrans;

		auto meshSelector = manager->tryGet<MeshSelectorEditorSystem>();
		if (meshSelector)
			meshSelector->skipUpdate();
	}

	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Gizmos", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));
		renderGizmosArrows(gizmosMeshes, backPipelineView, cameraConstants.viewProj, false);
		renderGizmosArrows(gizmosMeshes, frontPipelineView, cameraConstants.viewProj, true);
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Mesh Gizmos"))
	{
		auto settingsSystem = Manager::getInstance()->tryGet<SettingsSystem>();
		ImGui::Indent();
		ImGui::Checkbox("Enabled", &isEnabled);

		if (ImGui::ColorEdit4("Handle Color", &handleColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmosColor", handleColor);
		}
		if (ImGui::ColorEdit4("X-Axis Color", &axisColorX))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmosColorX", axisColorX);
		}
		if (ImGui::ColorEdit4("Y-Axis Color", &axisColorY))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmosColorY", axisColorY);
		}
		if (ImGui::ColorEdit4("Z-Axis Color", &axisColorZ))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmosColorZ", axisColorZ);
		}
		if (ImGui::DragFloat("Highlight Factor", &highlighFactor, 0.1f))
		{
			if (settingsSystem)
				settingsSystem->setFloat("meshGizmosFactor", highlighFactor);
		}
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif