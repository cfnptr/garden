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

#include "garden/editor/system/render/gizmos.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

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
		float4 color = float4(0.0f);
		ID<Buffer> vertexBuffer = {};
		float distance = 0.0f;
	};
}

//--------------------------------------------------------------------------------------------------
GizmosEditor::GizmosEditor(MeshRenderSystem* system)
{
	auto manager = system->getManager();
	auto graphicsSystem = system->getGraphicsSystem();
	auto resourceSystem = ResourceSystem::getInstance();
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto editorFramebuffer = deferredSystem->getEditorFramebuffer();
	frontGizmosPipeline = resourceSystem->loadGraphicsPipeline(
		"editor/gizmos-front", editorFramebuffer);
	backGizmosPipeline = resourceSystem->loadGraphicsPipeline(
		"editor/gizmos-back", editorFramebuffer);
	fullArrowVertices = graphicsSystem->createBuffer(Buffer::Bind::Vertex |
		Buffer::Bind::TransferDst, Buffer::Access::None, fullArrowVert,
		0, 0, Buffer::Usage::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, fullArrowVertices,
		"buffer.vertex.gizmos.arrow");
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
static void addArrowMeshes(vector<GizmosMesh>& gizmosMeshes,
	const float4x4& model, ID<Buffer> fullCube, ID<Buffer> fullArrow)
{
	GizmosMesh gizmosMesh;
	gizmosMesh.vertexBuffer = fullCube;
	gizmosMesh.model = model * scale(float3(0.1f, 0.1f, 0.1f));
	gizmosMesh.color = float4(0.9f, 0.9f, 0.9f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = fullArrow;
	gizmosMesh.model = model * translate(float3(0.9f, 0.0f, 0.0f)) *
		rotate(quat(radians(90.0f), float3::back)) * scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = float4(0.9f, 0.1f, 0.1f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.9f, 0.0f)) *
		scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = float4(0.1f, 0.9f, 0.1f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.0f, 0.9f)) *
		rotate(quat(radians(90.0f), float3::right)) * scale(float3(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = float4(0.1f, 0.1f, 0.9f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = fullCube;
	gizmosMesh.model = model * translate(float3(0.425f, 0.0f, 0.0f)) *
		scale(float3(0.75f, 0.05f, 0.05f));
	gizmosMesh.color = float4(0.9f, 0.1f, 0.1f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.425f, 0.0f)) *
		scale(float3(0.05f, 0.75f, 0.05f));
	gizmosMesh.color = float4(0.1f, 0.9f, 0.1f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = model * translate(float3(0.0f, 0.0f, 0.425f)) *
		scale(float3(0.05f, 0.05f, 0.75f));
	gizmosMesh.color = float4(0.1f, 0.1f, 0.9f, 1.0f);
	gizmosMeshes.push_back(gizmosMesh);
}

//--------------------------------------------------------------------------------------------------
static void renderGizmosArrows(GraphicsSystem* graphicsSystem,
	vector<GizmosMesh>& gizmosMeshes, View<GraphicsPipeline> pipelineView,
	float renderScale, const float4& viewportScissor,
	const float4x4& viewProj, bool sortAscend)
{
	function<bool(const GizmosMesh& a, const GizmosMesh& b)> ascending =
		[](const GizmosMesh& a, const GizmosMesh& b) { return a.distance < b.distance; };
	function<bool(const GizmosMesh& a, const GizmosMesh& b)> descending =
		[](const GizmosMesh& a, const GizmosMesh& b) { return a.distance > b.distance; };
	sort(gizmosMeshes.begin(), gizmosMeshes.end(), sortAscend ? ascending : descending);

	pipelineView->bind();
	pipelineView->setViewportScissor(viewportScissor);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->renderScale = 0.25f / renderScale;
	
	for (auto& mesh : gizmosMeshes)
	{
		auto bufferView = graphicsSystem->get(mesh.vertexBuffer);
		pushConstants->mvp = viewProj * mesh.model;
		pushConstants->color = mesh.color;
		pipelineView->pushConstants();
		
		pipelineView->draw(mesh.vertexBuffer, (uint32)(
			bufferView->getBinarySize() / sizeof(float3)));
	}
}

//--------------------------------------------------------------------------------------------------
void GizmosEditor::preSwapchainRender()
{
	auto manager = system->getManager();
	auto graphicsSystem = system->getGraphicsSystem();
	auto frontPipelineView = graphicsSystem->get(frontGizmosPipeline);
	auto backPipelineView = graphicsSystem->get(backGizmosPipeline);
	auto selectedEntity = EditorRenderSystem::getInstance()->selectedEntity;
	auto fullCubeVertices = graphicsSystem->getFullCubeVertices();
	auto fullCubeView = graphicsSystem->get(fullCubeVertices);
	auto fullArrowView = graphicsSystem->get(fullArrowVertices);

	if (lastLmbState && !graphicsSystem->isMouseButtonPressed(MouseButton::Left))
		lastLmbState = false; // Note: should be here to prevent selection freeze.

	if (!selectedEntity || !frontPipelineView->isReady() ||
		!backPipelineView->isReady() || !fullCubeView->isReady() ||
		!fullArrowView->isReady() || !graphicsSystem->camera)
	{
		return;
	}

	auto transform = manager->tryGet<TransformComponent>(selectedEntity);
	if (!transform || transform->hasBaked())
		return;

	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto model = transform->calcModel();
	setTranslation(model, getTranslation(model) - cameraPosition);
	auto translation = getTranslation(model);
	if (translation == float3(0.0f))
		return;

	auto windowSize = graphicsSystem->getWindowSize();
	auto cursorPosition = graphicsSystem->getCursorPosition();
	auto rotation =
		(graphicsSystem->isKeyboardButtonPressed(KeyboardButton::LeftShift) ||
		graphicsSystem->isKeyboardButtonPressed(KeyboardButton::RightShift)) &&
		graphicsSystem->getCursorMode() == CursorMode::Default ?
		quat::identity : extractQuat(extractRotation(model));
	auto distance = length(translation) * 0.25f;
	model = calcModel(translation, rotation, float3(distance));

	vector<GizmosMesh> gizmosMeshes;
	addArrowMeshes(gizmosMeshes, model, fullCubeVertices, fullArrowVertices);
	// TODO: scale and rotation gizmos.
	
	if (!ImGui::GetIO().WantCaptureMouse && graphicsSystem->getCursorMode() == CursorMode::Default)
	{
		auto uvPosition = (cursorPosition + 0.5f) / windowSize;
		auto globalDirection = (float3)(cameraConstants.viewProjInv *
			float4(uvPosition * 2.0f - 1.0f, 0.0f, 1.0f));

		float newDistance = FLT_MAX; uint32 meshIndex = 0;
		for (uint32 i = 0; i < 4; i++)
		{
			auto modelInverse = inverse(gizmosMeshes[i].model);
			auto localOrigin = modelInverse * float4(0.0f, 0.0f, 0.0f, 1.0f);
			auto localDirection = float3x3(modelInverse) * globalDirection;
			auto ray = Ray((float3)localOrigin, (float3)localDirection);
			auto points = raycast2(Aabb::one, ray);
			if (!isIntersected(points))
				continue;
			
			if (points.x < newDistance)
			{
				meshIndex = i;
				newDistance = points.x;
			}
		}

		if (newDistance != FLT_MAX)
		{
			auto& mesh = gizmosMeshes[meshIndex];
			mesh.model *= scale(float3(1.25f));

			switch (meshIndex)
			{
			case 0: mesh.color = float4(2.0f, 2.0f, 2.0f, 1.0f); break;
			case 1: mesh.color = float4(2.0f, 0.4f, 0.4f, 1.0f); break;
			case 2: mesh.color = float4(0.4f, 2.0f, 0.4f, 1.0f); break;
			case 3: mesh.color = float4(0.4f, 0.4f, 2.0f, 1.0f); break;
			default: abort();
			}

			if (!lastLmbState && graphicsSystem->isMouseButtonPressed(MouseButton::Left))
			{
				lastCursorPos = graphicsSystem->getCursorPosition();
				dragMode = meshIndex;
				lastLmbState = true;
			}
		}
	}
	
	if (lastLmbState)
	{
		auto uvPosition = (lastCursorPos + 0.5f) / windowSize;
		auto globalLastPos = (float3)(cameraConstants.viewProjInv *
			float4(uvPosition * 2.0f - 1.0f, 0.0f, 1.0f));
		uvPosition = (cursorPosition + 0.5f) / windowSize;
		auto globalNewPos = (float3)(cameraConstants.viewProjInv *
			float4(uvPosition * 2.0f - 1.0f, 0.0f, 1.0f));
		lastCursorPos = cursorPosition;

		auto cursorTrans = (globalNewPos -
			globalLastPos) * length(translation);
		switch (dragMode)
		{
		case 1: cursorTrans.y = cursorTrans.z = 0.0f; break;
		case 2: cursorTrans.x = cursorTrans.z = 0.0f; break;
		case 3: cursorTrans.x = cursorTrans.y = 0.0f; break;
		}

		if (dragMode != 0 &&
			!graphicsSystem->isKeyboardButtonPressed(KeyboardButton::LeftShift) &&
			!graphicsSystem->isKeyboardButtonPressed(KeyboardButton::RightShift))
		{
			cursorTrans = (float3x3)rotate(rotation) * cursorTrans;
		}

		transform->position += cursorTrans;
	}
	
	auto deferredSystem = manager->get<DeferredRenderSystem>();
	auto framebufferView = graphicsSystem->get(deferredSystem->getEditorFramebuffer());
	auto viewportScissor = float4(float2(0), deferredSystem->getFramebufferSize());
	auto renderScale = deferredSystem->getRenderScale();

	for (auto& mesh : gizmosMeshes)
		mesh.distance = length2(getTranslation(mesh.model));

	SET_GPU_DEBUG_LABEL("Gizmos", Color::transparent);
	framebufferView->beginRenderPass(float4(0.0f));

	renderGizmosArrows(graphicsSystem, gizmosMeshes,
		backPipelineView, renderScale, viewportScissor,
		cameraConstants.viewProj, false);
	renderGizmosArrows(graphicsSystem, gizmosMeshes,
		frontPipelineView, renderScale, viewportScissor,
		cameraConstants.viewProj, true);

	framebufferView->endRenderPass();
}
#endif