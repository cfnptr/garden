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

#include "garden/system/render/mesh.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/graphics.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
MeshRenderSystem::MeshRenderSystem(bool useAsyncRecording, bool useAsyncPreparing, bool setSingleton) :
	Singleton(setSingleton), asyncRecording(useAsyncRecording), asyncPreparing(useAsyncPreparing)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", MeshRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", MeshRenderSystem::deinit);
}
MeshRenderSystem::~MeshRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", MeshRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", MeshRenderSystem::deinit);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::init()
{
	if (ForwardRenderSystem::Instance::has())
	{
		ECSM_SUBSCRIBE_TO_EVENT("PreForwardRender", MeshRenderSystem::preForwardRender);
		ECSM_SUBSCRIBE_TO_EVENT("ForwardRender", MeshRenderSystem::forwardRender);
	}
	if (DeferredRenderSystem::Instance::has())
	{
		ECSM_SUBSCRIBE_TO_EVENT("PreDeferredRender", MeshRenderSystem::preDeferredRender);
		ECSM_SUBSCRIBE_TO_EVENT("DeferredRender", MeshRenderSystem::deferredRender);
		ECSM_SUBSCRIBE_TO_EVENT("MetaHdrRender", MeshRenderSystem::metaHdrRender);
	}
}
void MeshRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		if (ForwardRenderSystem::Instance::has())
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreForwardRender", MeshRenderSystem::preForwardRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("ForwardRender", MeshRenderSystem::forwardRender);
		}
		if (DeferredRenderSystem::Instance::has())
		{
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", MeshRenderSystem::preDeferredRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("DeferredRender", MeshRenderSystem::deferredRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("MetaHdrRender", MeshRenderSystem::metaHdrRender);
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareSystems()
{
	auto manager = Manager::Instance::get();
	const auto& systems = manager->getSystems();
	meshSystems.clear();

	for (const auto& pair : systems)
	{
		auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
		if (meshSystem)
			meshSystems.push_back(meshSystem);
	}

	#if GARDEN_EDITOR
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	if (graphicsEditorSystem)
	{
		graphicsEditorSystem->opaqueDrawCount = graphicsEditorSystem->opaqueTotalCount =
			graphicsEditorSystem->translucentDrawCount = graphicsEditorSystem->translucentTotalCount = 0;
	}
	#endif
}

//**********************************************************************************************************************
static void prepareOpaqueMeshes(const float3& cameraOffset, const float3& cameraPosition, 
	const Plane* frustumPlanes, MeshRenderSystem::OpaqueBuffer* opaqueBuffer, uint32 itemOffset, 
	uint32 itemCount, uint32 threadIndex, bool isShadowPass, bool useThreading)
{
	SET_CPU_ZONE_SCOPED("Opaque Meshes Prepare");

	auto transformSystem = TransformSystem::Instance::get();
	auto meshSystem = opaqueBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();

	MeshRenderSystem::OpaqueMesh* meshes = nullptr;
	if (useThreading)
	{
		auto& threadMeshes = opaqueBuffer->threadMeshes[threadIndex];
		if (threadMeshes.size() < itemCount - itemOffset)
			threadMeshes.resize(itemCount - itemOffset);
		meshes = threadMeshes.data();
	}
	else
	{
		meshes = opaqueBuffer->combinedMeshes.data();
	}

	uint32 drawIndex = 0;
	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
			continue;

		float4x4 model;
		const auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActive())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = float4x4::identity;
		}

		if (isBehindFrustum(meshRenderView->aabb, model, frustumPlanes, Plane::frustumCount))
		{
			if (!isShadowPass)
				meshRenderView->setVisible(false);
			continue;
		}

		if (!isShadowPass)
			meshRenderView->setVisible(true);

		// TODO: optimize this using SpatialDB.
		// Or we can potentially extract BVH from the PhysX or Vulkan?
		// TODO: we can use full scene BVH to speed up frustum culling.

		MeshRenderSystem::OpaqueMesh opaqueMesh;
		opaqueMesh.renderView = meshRenderView;
		opaqueMesh.model = (float4x3)model;
		opaqueMesh.distance2 = length2(getTranslation(model) + cameraOffset);
		meshes[drawIndex++] = opaqueMesh;
	}

	auto drawOffset = opaqueBuffer->drawCount->fetch_add(drawIndex);
	if (useThreading)
	{
		memcpy(opaqueBuffer->combinedMeshes.data() + drawOffset,
			meshes, drawIndex * sizeof(MeshRenderSystem::OpaqueMesh));
	}
}

//**********************************************************************************************************************
static void prepareTranslucentMeshes(const float3& cameraOffset, const float3& cameraPosition,
	const Plane* frustumPlanes, MeshRenderSystem::TranslucentBuffer* translucentBuffer, 
	MeshRenderSystem::TranslucentMesh* combinedMeshes, vector<vector<MeshRenderSystem::TranslucentMesh>>& threadMeshes,
	atomic<uint32>& translucentIndex, uint32 bufferIndex, uint32 itemOffset, 
	uint32 itemCount, uint32 threadIndex, bool isShadowPass, bool useThreading)
{
	SET_CPU_ZONE_SCOPED("Translucent Meshes Prepare");

	auto transformSystem = TransformSystem::Instance::get();
	auto meshSystem = translucentBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto drawCount = translucentBuffer->drawCount;

	MeshRenderSystem::TranslucentMesh* meshes = nullptr;
	if (useThreading)
	{
		auto& _threadMeshes = threadMeshes[threadIndex];
		if (_threadMeshes.size() < itemCount - itemOffset)
			_threadMeshes.resize(itemCount - itemOffset);
		meshes = _threadMeshes.data();
	}
	else
	{
		meshes = combinedMeshes;
	}

	uint32 drawIndex = 0;
	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
			continue;

		float4x4 model;
		const auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActive())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = float4x4::identity;
		}

		if (isBehindFrustum(meshRenderView->aabb, model, frustumPlanes, Plane::frustumCount))
		{
			if (!isShadowPass)
				meshRenderView->setVisible(false);
			continue;
		}

		if (!isShadowPass)
			meshRenderView->setVisible(true);

		MeshRenderSystem::TranslucentMesh translucentMesh;
		translucentMesh.renderView = meshRenderView;
		translucentMesh.model = (float4x3)model;
		translucentMesh.distance2 = length2(getTranslation(model) + cameraOffset);
		translucentMesh.bufferIndex = bufferIndex;
		meshes[drawIndex++] = translucentMesh;
	}

	auto drawOffset = translucentIndex.fetch_add(drawIndex);
	if (useThreading)
		memcpy(combinedMeshes + drawOffset, meshes, drawIndex * sizeof(MeshRenderSystem::TranslucentMesh));
	drawCount->fetch_add(drawIndex);
}

//**********************************************************************************************************************
void MeshRenderSystem::sortMeshes() // TODO: We can use here async bitonic sorting algorithm
{
	SET_CPU_ZONE_SCOPED("Meshes Sort");

	auto threadSystem = asyncPreparing ? ThreadSystem::Instance::tryGet() : nullptr;
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBuffers[i];
		if (opaqueBuffer->drawCount->load() == 0)
			continue;

		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([opaqueBuffer](const ThreadPool::Task& task) // Do not optimize args!
			{
				SET_CPU_ZONE_SCOPED("Opaque Meshes Sort");
				auto& meshes = opaqueBuffer->combinedMeshes;
				std::sort(meshes.begin(), meshes.begin() + opaqueBuffer->drawCount->load());
			});
		}
		else
		{
			auto& meshes = opaqueBuffer->combinedMeshes;
			std::sort(meshes.begin(), meshes.begin() + opaqueBuffer->drawCount->load());
		}
	}

	if (translucentIndex.load() > 0)
	{
		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([this](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Translucent Meshes Sort");
				std::sort(transCombinedMeshes.begin(), transCombinedMeshes.begin() + translucentIndex.load());
			});
		}
		else
		{
			std::sort(transCombinedMeshes.begin(), transCombinedMeshes.begin() + translucentIndex.load());
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareMeshes(const float4x4& viewProj, 
	const float3& cameraOffset, uint8 frustumPlaneCount, bool isShadowPass)
{
	SET_CPU_ZONE_SCOPED("Meshes Prepare");

	uint32 translucentMaxCount = 0;
	translucentIndex.store(0);
	opaqueBufferCount = translucentBufferCount = 0;

	for (auto meshSystem : meshSystems)
	{
		auto renderType = meshSystem->getMeshRenderType();
		if (renderType == MeshRenderType::Opaque)
		{
			opaqueBufferCount++;
		}
		else if (renderType == MeshRenderType::Translucent)
		{
			const auto& componentPool = meshSystem->getMeshComponentPool();
			translucentMaxCount += componentPool.getCount();
			translucentBufferCount++;
		}
	}

	if (opaqueBuffers.size() < opaqueBufferCount)
		opaqueBuffers.resize(opaqueBufferCount);
	if (translucentBuffers.size() < translucentBufferCount)
		translucentBuffers.resize(translucentBufferCount);
	if (transCombinedMeshes.size() < translucentMaxCount)
		transCombinedMeshes.resize(translucentMaxCount);

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto threadSystem = asyncPreparing ? ThreadSystem::Instance::tryGet() : nullptr;
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	uint32 opaqueBufferIndex = 0, translucentBufferIndex = 0;
	Plane frustumPlanes[Plane::frustumCount];
	extractFrustumPlanes(viewProj, frustumPlanes);

	#if GARDEN_EDITOR
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	#endif

	for (auto meshSystem : meshSystems)
	{
		const auto& componentPool = meshSystem->getMeshComponentPool();
		auto componentCount = componentPool.getCount();
		auto renderType = meshSystem->getMeshRenderType();
		
		if (renderType == MeshRenderType::Opaque)
		{
			auto opaqueBuffer = &opaqueBuffers[opaqueBufferIndex++];
			opaqueBuffer->meshSystem = meshSystem;
			opaqueBuffer->drawCount->store(0);

			if (componentCount == 0 || !meshSystem->isDrawReady(isShadowPass))
				continue;

			if (opaqueBuffer->combinedMeshes.size() < componentCount)
				opaqueBuffer->combinedMeshes.resize(componentCount);
			
			if (threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				if (opaqueBuffer->threadMeshes.size() < threadPool.getThreadCount())
					opaqueBuffer->threadMeshes.resize(threadPool.getThreadCount());

				threadPool.addItems([&cameraOffset, &cameraPosition, frustumPlanes, opaqueBuffer, isShadowPass]
					(const ThreadPool::Task& task) // Do not optimize args!
				{
					prepareOpaqueMeshes(cameraOffset, cameraPosition, frustumPlanes, opaqueBuffer, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), isShadowPass, true);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareOpaqueMeshes(cameraOffset, cameraPosition, frustumPlanes,
					opaqueBuffer, 0, componentPool.getOccupancy(), 0, isShadowPass, false);
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->opaqueTotalCount += componentCount;
			#endif	
		}
		else if (renderType == MeshRenderType::Translucent)
		{
			auto bufferIndex = translucentBufferIndex++;
			auto translucentBuffer = &translucentBuffers[bufferIndex];
			translucentBuffer->meshSystem = meshSystem;
			translucentBuffer->drawCount->store(0);

			if (componentCount == 0 || !meshSystem->isDrawReady(isShadowPass))
				continue;

			if (threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				if (transThreadMeshes.size() < threadPool.getThreadCount())
					transThreadMeshes.resize(threadPool.getThreadCount());

				threadSystem->getForegroundPool().addItems([this, &cameraOffset, &cameraPosition, frustumPlanes, 
					translucentBuffer, bufferIndex, isShadowPass](const ThreadPool::Task& task) // Do not optimize args!
				{
					prepareTranslucentMeshes(cameraOffset, cameraPosition, frustumPlanes, translucentBuffer, 
						transCombinedMeshes.data(), transThreadMeshes, translucentIndex, bufferIndex, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), isShadowPass, true);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareTranslucentMeshes(cameraOffset, cameraPosition, frustumPlanes, translucentBuffer, 
					transCombinedMeshes.data(), transThreadMeshes, translucentIndex, bufferIndex, 
					0, componentPool.getOccupancy(), 0, isShadowPass, false);
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->translucentTotalCount += componentCount;
			#endif	
		}
	}		

	if (threadSystem)
		threadSystem->getForegroundPool().wait();

	#if GARDEN_EDITOR
	if (graphicsEditorSystem)
	{
		for (uint32 i = 0; i < opaqueBufferCount; i++)
		{
			auto& opaqueBuffer = opaqueBuffers[i];
			graphicsEditorSystem->opaqueDrawCount += opaqueBuffer.drawCount->load();
		}

		graphicsEditorSystem->translucentDrawCount += translucentIndex.load();
	}
	#endif	

	sortMeshes();

	if (threadSystem)
		threadSystem->getForegroundPool().wait();
}

//**********************************************************************************************************************
void MeshRenderSystem::renderOpaque(const float4x4& viewProj, bool isShadowPass)
{
	SET_CPU_ZONE_SCOPED("Opaque Mesh Render");

	auto threadSystem = asyncRecording ? ThreadSystem::Instance::tryGet() : nullptr;
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBuffers[i];
		auto drawCount = opaqueBuffer->drawCount->load();
		if (drawCount == 0)
			continue;

		auto meshSystem = opaqueBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, drawCount, isShadowPass);

		if (threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems([opaqueBuffer, &viewProj](const ThreadPool::Task& task)
			{
				auto meshSystem = opaqueBuffer->meshSystem;
				const auto& meshes = opaqueBuffer->combinedMeshes;
				auto itemCount = task.getItemCount();
				auto taskIndex = task.getTaskIndex(); // Using task index to preserve items order.
				auto taskCount = itemCount - task.getItemOffset();

				meshSystem->beginDrawAsync(taskIndex);
				for (uint32 j = task.getItemOffset(); j < itemCount; j++)
				{
					const auto& mesh = meshes[j];
					auto model = float4x4(mesh.model, 0.0f, 0.0f, 0.0f, 1.0f);
					meshSystem->drawAsync(mesh.renderView, viewProj, model, j, taskIndex);
				}
				meshSystem->endDrawAsync(taskCount, taskIndex);
			},
			drawCount);
			threadPool.wait(); // Required
		}
		else
		{
			const auto& meshes = opaqueBuffer->combinedMeshes;
			meshSystem->beginDrawAsync(-1);
			for (uint32 j = 0; j < drawCount; j++)
			{
				const auto& mesh = meshes[j];
				auto model = float4x4(mesh.model, 0.0f, 0.0f, 0.0f, 1.0f);
				meshSystem->drawAsync(mesh.renderView, viewProj, model, j, -1);
			}
			meshSystem->endDrawAsync(drawCount, -1);
		}

		meshSystem->finalizeDraw(viewProj, drawCount, isShadowPass);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderTranslucent(const float4x4& viewProj, bool isShadowPass)
{
	SET_CPU_ZONE_SCOPED("Translucent Mesh Render");

	auto drawCount = translucentIndex.load();
	if (drawCount == 0)
		return;

	for (uint32 i = 0; i < translucentBufferCount; i++)
	{
		auto& translucentBuffer = translucentBuffers[i];
		auto bufferDrawCount = translucentBuffer.drawCount->load();

		if (bufferDrawCount == 0)
			continue;

		auto meshSystem = translucentBuffer.meshSystem;
		meshSystem->prepareDraw(viewProj, bufferDrawCount, isShadowPass);
		translucentBuffer.drawCount->store(0);
	}

	auto threadSystem = asyncRecording ? ThreadSystem::Instance::tryGet() : nullptr;
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([this, &viewProj](const ThreadPool::Task& task)
		{
			auto currentBufferIndex = transCombinedMeshes[task.getItemOffset()].bufferIndex;
			auto meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
			auto bufferDrawCount = translucentBuffers[currentBufferIndex].drawCount;
			auto itemCount = task.getItemCount();
			auto taskIndex = task.getTaskIndex(); // Using task index to preserve items order.
			meshSystem->beginDrawAsync(taskIndex);

			uint32 currentDrawCount = 0;
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				const auto& mesh = transCombinedMeshes[i];
				if (currentBufferIndex != mesh.bufferIndex)
				{
					meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
					meshSystem->endDrawAsync(currentDrawCount, taskIndex);

					currentBufferIndex = mesh.bufferIndex;
					currentDrawCount = 0;

					bufferDrawCount = translucentBuffers[currentBufferIndex].drawCount;
					meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
					meshSystem->beginDrawAsync(taskIndex);
				}

				auto drawIndex = bufferDrawCount->fetch_add(1);
				auto model = float4x4(mesh.model, 0.0f, 0.0f, 0.0f, 1.0f);
				meshSystem->drawAsync(mesh.renderView, viewProj, model, drawIndex, taskIndex);
				currentDrawCount++;
			}

			meshSystem->endDrawAsync(currentDrawCount, taskIndex);
		},
		drawCount);
		threadPool.wait(); // Required
	}
	else
	{
		auto currentBufferIndex = transCombinedMeshes[0].bufferIndex;
		auto meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
		auto bufferDrawCount = translucentBuffers[currentBufferIndex].drawCount;
		meshSystem->beginDrawAsync(-1);

		uint32 currentDrawCount = 0;
		for (uint32 i = 0; i < drawCount; i++)
		{
			const auto& mesh = transCombinedMeshes[i];
			if (currentBufferIndex != mesh.bufferIndex)
			{
				meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
				meshSystem->endDrawAsync(currentDrawCount, -1);

				currentBufferIndex = mesh.bufferIndex;
				currentDrawCount = 0;

				bufferDrawCount = translucentBuffers[currentBufferIndex].drawCount;
				meshSystem = translucentBuffers[currentBufferIndex].meshSystem;
				meshSystem->beginDrawAsync(-1);
			}

			auto drawIndex = bufferDrawCount->fetch_add(1);
			auto model = float4x4(mesh.model, 0.0f, 0.0f, 0.0f, 1.0f);
			meshSystem->drawAsync(mesh.renderView, viewProj, model, drawIndex, -1);
			currentDrawCount++;
		}

		meshSystem->endDrawAsync(currentDrawCount, -1);
	}

	for (uint32 i = 0; i < translucentBufferCount; i++)
	{
		auto& translucentBuffer = translucentBuffers[i];
		auto drawCount = translucentBuffer.drawCount->load();
		if (drawCount == 0)
			continue;

		auto meshSystem = translucentBuffer.meshSystem;
		meshSystem->finalizeDraw(viewProj, drawCount, isShadowPass);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderShadows()
{
	SET_CPU_ZONE_SCOPED("Shadows Mesh Render");
	SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);

	const auto& systems = Manager::Instance::get()->getSystems();
	for (const auto& pair : systems)
	{
		auto shadowSystem = dynamic_cast<IShadowMeshRenderSystem*>(pair.second);
		if (!shadowSystem)
			continue;

		auto passCount = shadowSystem->getShadowPassCount();
		for (uint32 i = 0; i < passCount; i++)
		{
			float4x4 viewProj; float3 cameraOffset;
			if (!shadowSystem->prepareShadowRender(i, viewProj, cameraOffset))
				continue;

			prepareMeshes(viewProj, cameraOffset, Plane::frustumCount, true);

			shadowSystem->beginShadowRender(i, MeshRenderType::Opaque);
			renderOpaque(viewProj, true);
			shadowSystem->endShadowRender(i, MeshRenderType::Opaque);

			shadowSystem->beginShadowRender(i, MeshRenderType::Translucent);
			renderTranslucent(viewProj, true);
			shadowSystem->endShadowRender(i, MeshRenderType::Translucent);
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::preForwardRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Forward Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	prepareSystems();
	renderShadows();

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareMeshes(cameraConstants.viewProj, float3(0.0f), Plane::frustumCount - 2, false);
}

void MeshRenderSystem::forwardRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Forward Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(cameraConstants.viewProj, false);
	renderTranslucent(cameraConstants.viewProj, false);
}

//**********************************************************************************************************************
void MeshRenderSystem::preDeferredRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Deferred Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	prepareSystems();
	renderShadows();

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareMeshes(cameraConstants.viewProj, float3(0.0f), Plane::frustumCount - 2, false);
}

void MeshRenderSystem::deferredRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Deferred Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(cameraConstants.viewProj, false);
}

void MeshRenderSystem::metaHdrRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Meta HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderTranslucent(cameraConstants.viewProj, false);
}