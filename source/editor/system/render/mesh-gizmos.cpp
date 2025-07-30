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

#include "garden/editor/system/render/mesh-gizmos.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/mesh-selector.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/character.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/resource/primitive.hpp"
#include "math/matrix/transform.hpp"
#include "math/angles.hpp"
#include <array>

using namespace garden;

// TODO: rewrite with winding arrow order CCW

constexpr array<float3, 18> arrowVertices =
{
	// -X side
	float3(-0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.0f,  0.5f,  0.0f),
	// -Z side
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.0f,  0.5f, -0.0f),
	// -Y side
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	// +X side
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.0f,  0.5f, -0.0f),
	// +Z side
	float3( 0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.0f,  0.5f,  0.0f),
};

//**********************************************************************************************************************
MeshGizmosEditorSystem::MeshGizmosEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", MeshGizmosEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", MeshGizmosEditorSystem::deinit);
}
MeshGizmosEditorSystem::~MeshGizmosEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(arrowVertexBuffer);
		graphicsSystem->destroy(backGizmosPipeline);
		graphicsSystem->destroy(frontGizmosPipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", MeshGizmosEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", MeshGizmosEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::init()
{
	if (DeferredRenderSystem::Instance::has())
		ECSM_SUBSCRIBE_TO_EVENT("DepthLdrRender", MeshGizmosEditorSystem::render);
	else
		ECSM_SUBSCRIBE_TO_EVENT("DepthForwardRender", MeshGizmosEditorSystem::render);
	ECSM_SUBSCRIBE_TO_EVENT("EditorSettings", MeshGizmosEditorSystem::editorSettings);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto resourceSystem = ResourceSystem::Instance::get();

	ID<Framebuffer> framebuffer;
	if (DeferredRenderSystem::Instance::has())
		framebuffer = DeferredRenderSystem::Instance::get()->getDepthLdrFramebuffer();
	else
		framebuffer = ForwardRenderSystem::Instance::get()->getFullFramebuffer();

	ResourceSystem::GraphicsOptions options;
	frontGizmosPipeline = resourceSystem->loadGraphicsPipeline("editor/gizmos-front", framebuffer, options);
	backGizmosPipeline = resourceSystem->loadGraphicsPipeline("editor/gizmos-back", framebuffer, options);

	graphicsSystem->getCubeVertexBuffer(); // Note: Allocating default cube in advance.
	arrowVertexBuffer = graphicsSystem->createBuffer(Buffer::Usage::Vertex | 
		Buffer::Usage::TransferDst | Buffer::Usage::TransferQ, Buffer::CpuAccess::None, 
		arrowVertices, 0, 0, Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(arrowVertexBuffer, "buffer.vertex.gizmos.arrow");

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		settingsSystem->getColor("meshGizmos.handleColor", handleColor);
		settingsSystem->getColor("meshGizmos.axisColorX", axisColorX);
		settingsSystem->getColor("meshGizmos.axisColorY", axisColorY);
		settingsSystem->getColor("meshGizmos.axisColorZ", axisColorZ);
		settingsSystem->getFloat("meshGizmos.highlightFactor", highlightFactor);
		settingsSystem->getFloat("meshGizmos.patternScale", patternScale);
	}
}
void MeshGizmosEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(arrowVertexBuffer);
		graphicsSystem->destroy(backGizmosPipeline);
		graphicsSystem->destroy(frontGizmosPipeline);

		if (DeferredRenderSystem::Instance::has())
			ECSM_UNSUBSCRIBE_FROM_EVENT("DepthLdrRender", MeshGizmosEditorSystem::render);
		else
			ECSM_UNSUBSCRIBE_FROM_EVENT("DepthForwardRender", MeshGizmosEditorSystem::render);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorSettings", MeshGizmosEditorSystem::editorSettings);
	}
}

//**********************************************************************************************************************
static void addGizmosMeshes(vector<MeshGizmosEditorSystem::GizmosMesh>& gizmosMeshes, const f32x4x4& model, 
	ID<Buffer> cubeBuffer, ID<Buffer> arrowBuffer, Color handleColor, Color axisColorX, Color axisColorY, Color axisColorZ)
{
	MeshGizmosEditorSystem::GizmosMesh gizmosMesh;
	gizmosMesh.vertexBuffer = cubeBuffer;
	gizmosMesh.vertexCount = primitive::cubeVertices.size();
	gizmosMesh.model = scale(model, f32x4(0.1f, 0.1f, 0.1f));
	gizmosMesh.color = handleColor;
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = arrowBuffer;
	gizmosMesh.vertexCount = arrowVertices.size();
	gizmosMesh.model = translate(model, f32x4(0.9f, 0.0f, 0.0f)) *
		rotate(quat(radians(90.0f), f32x4::back)) * scale(f32x4(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorX;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = scale(translate(model, f32x4(0.0f, 0.9f, 0.0f)), f32x4(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorY;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = translate(model, f32x4(0.0f, 0.0f, 0.9f)) *
		rotate(quat(radians(90.0f), f32x4::right)) * scale(f32x4(0.1f, 0.2f, 0.1f));
	gizmosMesh.color = axisColorZ;
	gizmosMeshes.push_back(gizmosMesh);

	gizmosMesh.vertexBuffer = cubeBuffer;
	gizmosMesh.vertexCount = primitive::cubeVertices.size();
	gizmosMesh.model = scale(translate(model, f32x4(0.425f, 0.0f, 0.0f)), f32x4(0.75f, 0.05f, 0.05f));
	gizmosMesh.color = axisColorX;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = scale(translate(model, f32x4(0.0f, 0.425f, 0.0f)), f32x4(0.05f, 0.75f, 0.05f));
	gizmosMesh.color = axisColorY;
	gizmosMeshes.push_back(gizmosMesh);
	gizmosMesh.model = scale(translate(model, f32x4(0.0f, 0.0f, 0.425f)), f32x4(0.05f, 0.05f, 0.75f));
	gizmosMesh.color = axisColorZ;
	gizmosMeshes.push_back(gizmosMesh);
}

//**********************************************************************************************************************
static void renderGizmosMeshes(vector<MeshGizmosEditorSystem::GizmosMesh>& gizmosMeshes, 
	View<GraphicsPipeline> pipelineView, const f32x4x4& viewProj, float patternScale, bool sortAscend)
{
	if (sortAscend)
	{
		sort(gizmosMeshes.begin(), gizmosMeshes.end(), [](
			const MeshGizmosEditorSystem::GizmosMesh& a, const MeshGizmosEditorSystem::GizmosMesh& b)
		{
			return a.distance < b.distance;
		});
	}
	else
	{
		sort(gizmosMeshes.begin(), gizmosMeshes.end(), [](
			const MeshGizmosEditorSystem::GizmosMesh& a, const MeshGizmosEditorSystem::GizmosMesh& b)
		{
			return a.distance > b.distance;
		});
	}

	pipelineView->bind();
	pipelineView->setViewportScissor();
	
	for (const auto& mesh : gizmosMeshes)
	{
		MeshGizmosEditorSystem::PushConstants pc;
		pc.mvp = (float4x4)(viewProj * mesh.model);
		pc.color = (float3)mesh.color;
		pc.patternScale = patternScale;
		pipelineView->pushConstants(&pc);
		pipelineView->draw(mesh.vertexBuffer, mesh.vertexCount);
	}
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::render()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto selectedEntity = EditorRenderSystem::Instance::get()->selectedEntity;
	if (!isEnabled || !selectedEntity || !graphicsSystem->canRender() || 
		!graphicsSystem->camera || selectedEntity == graphicsSystem->camera)
	{
		return;
	}

	auto inputSystem = InputSystem::Instance::get();
	if (!inputSystem->getMouseState(MouseButton::Left))
		dragMode = 0;
	
	auto manager = Manager::Instance::get();
	auto transformView = TransformSystem::Instance::get()->tryGetComponent(selectedEntity);
	auto frontPipelineView = graphicsSystem->get(frontGizmosPipeline);
	auto backPipelineView = graphicsSystem->get(backGizmosPipeline);
	auto cubeVertexBuffer = graphicsSystem->getCubeVertexBuffer();
	auto cubeVertexView = graphicsSystem->get(cubeVertexBuffer);
	auto arrowVertexView = graphicsSystem->get(arrowVertexBuffer);

	// TODO: check for selection freeze problem.

	if (!transformView || transformView->hasStaticWithDescendants() || !frontPipelineView->isReady() ||
		!backPipelineView->isReady() || !cubeVertexView->isReady() || !arrowVertexView->isReady())
	{
		return;
	}

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	auto model = transformView->calcModel(cameraConstants.cameraPos);
	
	auto windowSize = inputSystem->getWindowSize();
	auto cursorPosition = inputSystem->getCursorPosition();
	auto rotation = (inputSystem->getKeyboardState(KeyboardButton::LeftShift) ||
		inputSystem->getKeyboardState(KeyboardButton::RightShift)) &&
		inputSystem->getCursorMode() == CursorMode::Normal ?
		quat::identity : extractQuat(extractRotation(model));
	auto translation = getTranslation(model);

	auto modelScale = 0.25f;
	auto cameraView = manager->tryGet<CameraComponent>(graphicsSystem->camera);
	if (cameraView)
	{
		if (cameraView->type == ProjectionType::Perspective)
		{
			if (f32x4(translation, 0.0f) != f32x4::zero)
				modelScale *= length3(translation);
		}
		else
		{
			modelScale *= (cameraView->p.orthographic.height.y - 
				cameraView->p.orthographic.height.x) * 0.5f;
		}
	}
	model = calcModel(translation, rotation, f32x4(modelScale));

	addGizmosMeshes(gizmosMeshes, model, cubeVertexBuffer, arrowVertexBuffer,
		handleColor, axisColorX, axisColorY, axisColorZ);
	// TODO: scale and rotation gizmos.
	
	if (!ImGui::GetIO().WantCaptureMouse && inputSystem->getCursorMode() == CursorMode::Normal)
	{
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalOrigin = cameraConstants.invViewProj * f32x4(ndcPosition.x, ndcPosition.y, 1.0f, 1.0f);
		auto globalDirection = cameraConstants.invViewProj * f32x4(ndcPosition.x, ndcPosition.y, 0.0001f, 1.0f);
		globalOrigin /= globalOrigin.getW();
		globalDirection = globalDirection / globalDirection.getW() - globalOrigin;
		
		float newDistSq = FLT_MAX; uint32 meshIndex = 0;
		for (uint32 i = 0; i < 4; i++)
		{
			auto modelInverse = inverse4x4(gizmosMeshes[i].model);
			auto ray = Ray(modelInverse * f32x4(globalOrigin, 1.0f), multiply3x3(modelInverse, globalDirection));
			if (!raycast(Aabb::two, ray))
				continue;

			auto distSq = distanceSq3(globalOrigin, getTranslation(gizmosMeshes[i].model));
			if (distSq < newDistSq)
			{
				meshIndex = i;
				newDistSq = distSq;
			}
		}

		if (newDistSq != FLT_MAX)
		{
			auto& mesh = gizmosMeshes[meshIndex];
			mesh.model = scale(mesh.model, f32x4(1.25f));
			mesh.color = Color((float4)mesh.color * highlightFactor);

			if (inputSystem->isMousePressed(MouseButton::Left))
				dragMode = meshIndex + 1;
		}
	}

	for (auto& mesh : gizmosMeshes)
		mesh.distance = lengthSq3(getTranslation(mesh.model));

	if (dragMode != 0)
	{
		auto cursorPosition = inputSystem->getCursorPosition();
		auto ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalLastPos = cameraConstants.invViewProj * 
			f32x4(ndcPosition.x, ndcPosition.y, 0.0f, 1.0f);
		cursorPosition += inputSystem->getCursorDelta();
		ndcPosition = ((cursorPosition + 0.5f) / windowSize) * 2.0f - 1.0f;
		auto globalNewPos = cameraConstants.invViewProj * 
			f32x4(ndcPosition.x, ndcPosition.y, 0.0f, 1.0f);

		if (dragMode != 1)
		{
			auto invModel = inverseTransRot(calcModel(translation, rotation));
			globalLastPos = multiply3x3(invModel, globalLastPos);
			globalNewPos = multiply3x3(invModel, globalNewPos);
		}

		auto cursorTrans = (globalNewPos - globalLastPos); 
		switch (dragMode)
		{
		case 2: cursorTrans.setY(0.0f); cursorTrans.setZ(0.0f); break;
		case 3: cursorTrans.setX(0.0f); cursorTrans.setZ(0.0f); break;
		case 4: cursorTrans.setX(0.0f); cursorTrans.setY(0.0f); break;
		}

		if (dragMode != 1 && !inputSystem->getKeyboardState(KeyboardButton::LeftShift) &&
			!inputSystem->getKeyboardState(KeyboardButton::RightShift))
		{
			cursorTrans = multiply3x3(rotate(transformView->getRotation()), cursorTrans);
		}

		// TODO: Fix non-uniform transformation in perspective projection,
		//       take into account perspective distortion if possible.
		if (cameraView->type == ProjectionType::Perspective)
			cursorTrans *= length3(translation);
		transformView->translate(cursorTrans);

		auto rigidbodyView = manager->tryGet<RigidbodyComponent>(selectedEntity);
		if (rigidbodyView && rigidbodyView->getShape())
		{
			// Note: We can also move Dynamic body with moveKinematic, but no need here.
			if (rigidbodyView->getMotionType() == MotionType::Kinematic)
			{
				rigidbodyView->moveKinematic(rigidbodyView->getPosition() + cursorTrans,
					rigidbodyView->getRotation(), inputSystem->getDeltaTime());
			}
			else
			{
				rigidbodyView->setPosition(rigidbodyView->getPosition() + cursorTrans, false);
			}
		}

		auto characterView = manager->tryGet<CharacterComponent>(selectedEntity);
		if (characterView && characterView->getShape())
			characterView->setPosition(characterView->getPosition() + cursorTrans);

		auto meshSelector = manager->tryGet<MeshSelectorEditorSystem>();
		if (meshSelector)
			meshSelector->skipUpdate();
	}

	SET_GPU_DEBUG_LABEL("Gizmos", Color::transparent);
	renderGizmosMeshes(gizmosMeshes, backPipelineView, cameraConstants.viewProj, patternScale, false);
	renderGizmosMeshes(gizmosMeshes, frontPipelineView, cameraConstants.viewProj, patternScale, true);
	gizmosMeshes.clear();
}

//**********************************************************************************************************************
void MeshGizmosEditorSystem::editorSettings()
{
	if (ImGui::CollapsingHeader("Mesh Gizmos"))
	{
		ImGui::Indent();
		ImGui::PushID("meshGizmos");

		ImGui::Checkbox("Enabled", &isEnabled);

		auto settingsSystem = SettingsSystem::Instance::tryGet();
		if (ImGui::ColorEdit4("Handle Color", &handleColor))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmos.handleColor", handleColor);
		}
		if (ImGui::ColorEdit4("X-Axis Color", &axisColorX))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmos.axisColorX", axisColorX);
		}
		if (ImGui::ColorEdit4("Y-Axis Color", &axisColorY))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmos.axisColorY", axisColorY);
		}
		if (ImGui::ColorEdit4("Z-Axis Color", &axisColorZ))
		{
			if (settingsSystem)
				settingsSystem->setColor("meshGizmos.axisColorZ", axisColorZ);
		}
		if (ImGui::DragFloat("Highlight Factor", &highlightFactor, 0.1f))
		{
			if (settingsSystem)
				settingsSystem->setFloat("meshGizmos.highlightFactor", highlightFactor);
		}
		if (ImGui::DragFloat("Pattern Scale", &patternScale, 0.01f))
		{
			if (settingsSystem)
				settingsSystem->setFloat("meshGizmos.patternScale", patternScale);
		}

		ImGui::PopID();
		ImGui::Unindent();
		ImGui::Spacing();
	}
}
#endif