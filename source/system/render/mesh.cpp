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
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/graphics.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
MeshRenderSystem::MeshRenderSystem(bool useOIT, bool useAsyncRecording, bool useAsyncPreparing, bool setSingleton) :
	Singleton(setSingleton), hasOIT(useOIT), asyncRecording(useAsyncRecording), asyncPreparing(useAsyncPreparing)
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
		ECSM_SUBSCRIBE_TO_EVENT("DepthHdrRender", MeshRenderSystem::depthHdrRender);
		ECSM_SUBSCRIBE_TO_EVENT("PreRefractedRender", MeshRenderSystem::preRefractedRender);
		ECSM_SUBSCRIBE_TO_EVENT("RefractedRender", MeshRenderSystem::refractedRender);
		ECSM_SUBSCRIBE_TO_EVENT("PreTransDepthRender", MeshRenderSystem::preTransDepthRender);
		ECSM_SUBSCRIBE_TO_EVENT("TransDepthRender", MeshRenderSystem::transDepthRender);

		if (hasOIT)
		{
			ECSM_SUBSCRIBE_TO_EVENT("PreOitRender", MeshRenderSystem::preOitRender);
			ECSM_SUBSCRIBE_TO_EVENT("OitRender", MeshRenderSystem::oitRender);
		}
		else
		{
			ECSM_SUBSCRIBE_TO_EVENT("Translucent", MeshRenderSystem::translucentRender);
		}
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
			ECSM_UNSUBSCRIBE_FROM_EVENT("DepthHdrRender", MeshRenderSystem::depthHdrRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreRefractedRender", MeshRenderSystem::preRefractedRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("RefractedRender", MeshRenderSystem::refractedRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreTransDepthRender", MeshRenderSystem::preTransDepthRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("TransDepthRender", MeshRenderSystem::transDepthRender);

			if (hasOIT)
			{
				ECSM_UNSUBSCRIBE_FROM_EVENT("PreOitRender", MeshRenderSystem::preOitRender);
				ECSM_UNSUBSCRIBE_FROM_EVENT("OitRender", MeshRenderSystem::oitRender);
			}
			else
			{
				ECSM_UNSUBSCRIBE_FROM_EVENT("Translucent", MeshRenderSystem::translucentRender);
			}
		}

		for (auto buffer : sortedBuffers)
			delete buffer;
		for (auto buffer : unsortedBuffers)
			delete buffer;
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareSystems()
{
	SET_CPU_ZONE_SCOPED("Systems Prepare");

	auto manager = Manager::Instance::get();
	const auto& systems = manager->getSystems();
	meshSystems.clear();

	if (isOpaqueOnly)
	{
		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (meshSystem && meshSystem->getMeshRenderType() == MeshRenderType::Opaque)
				meshSystems.push_back(meshSystem);
		}
	}
	else
	{
		for (const auto& pair : systems)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
			if (meshSystem)
				meshSystems.push_back(meshSystem);
		}
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
static void prepareUnsortedMeshes(f32x4 cameraOffset, f32x4 cameraPosition, 
	const Plane* frustumPlanes, MeshRenderSystem::UnsortedBuffer* unsortedBuffer, uint32 itemOffset, 
	uint32 itemCount, uint32 threadIndex, int8 shadowPass, bool useThreading)
{
	SET_CPU_ZONE_SCOPED("Unsorted Meshes Prepare");

	auto transformSystem = TransformSystem::Instance::get();
	auto meshSystem = unsortedBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto isNotShadowPass = shadowPass < 0;

	MeshRenderSystem::UnsortedMesh* meshes = nullptr;
	if (useThreading)
	{
		auto& threadMeshes = unsortedBuffer->threadMeshes[threadIndex];
		if (threadMeshes.size() < itemCount - itemOffset)
			threadMeshes.resize(itemCount - itemOffset);
		meshes = threadMeshes.data();
	}
	else
	{
		meshes = unsortedBuffer->combinedMeshes.data();
	}

	uint32 drawIndex = 0;
	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
			continue;

		f32x4x4 model;
		const auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActive())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = f32x4x4::identity;
		}

		if (isBehindFrustum(frustumPlanes, Plane::frustumCount, meshRenderView->aabb, model))
		{
			if (isNotShadowPass)
				meshRenderView->setVisible(false);
			continue;
		}

		if (isNotShadowPass)
			meshRenderView->setVisible(true);

		MeshRenderSystem::UnsortedMesh unsortedMesh;
		unsortedMesh.renderView = meshRenderView;
		unsortedMesh.model = (float4x3)model;
		unsortedMesh.distanceSq = lengthSq3(getTranslation(model) + cameraOffset);
		meshes[drawIndex++] = unsortedMesh;
	}

	auto drawOffset = unsortedBuffer->drawCount.fetch_add(drawIndex);
	if (useThreading)
	{
		memcpy(unsortedBuffer->combinedMeshes.data() + drawOffset,
			meshes, drawIndex * sizeof(MeshRenderSystem::UnsortedMesh));
	}
}

//**********************************************************************************************************************
static void prepareSortedMeshes(f32x4 cameraOffset, f32x4 cameraPosition, const Plane* frustumPlanes, 
	MeshRenderSystem::SortedBuffer* sortedBuffer, MeshRenderSystem::SortedMesh* combinedMeshes, 
	vector<vector<MeshRenderSystem::SortedMesh>>& threadMeshes, atomic<uint32>& sortedDrawIndex, 
	uint32 bufferIndex, uint32 itemOffset, uint32 itemCount, uint32 threadIndex, int8 shadowPass, bool useThreading)
{
	SET_CPU_ZONE_SCOPED("Sorted Meshes Prepare");

	auto transformSystem = TransformSystem::Instance::get();
	auto meshSystem = sortedBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto& drawCount = sortedBuffer->drawCount;
	auto isNotShadowPass = shadowPass < 0;

	MeshRenderSystem::SortedMesh* meshes = nullptr;
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

		f32x4x4 model;
		const auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActive())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = f32x4x4::identity;
		}

		if (isBehindFrustum(frustumPlanes, Plane::frustumCount, meshRenderView->aabb, model))
		{
			if (isNotShadowPass)
				meshRenderView->setVisible(false);
			continue;
		}

		if (isNotShadowPass)
			meshRenderView->setVisible(true);

		MeshRenderSystem::SortedMesh sortedMesh;
		sortedMesh.renderView = meshRenderView;
		sortedMesh.model = (float4x3)model;
		sortedMesh.distanceSq = lengthSq3(getTranslation(model) + cameraOffset);
		sortedMesh.bufferIndex = bufferIndex;
		meshes[drawIndex++] = sortedMesh;
	}

	auto drawOffset = sortedDrawIndex.fetch_add(drawIndex);
	if (useThreading)
		memcpy(combinedMeshes + drawOffset, meshes, drawIndex * sizeof(MeshRenderSystem::SortedMesh));
	drawCount.fetch_add(drawIndex);
}

//**********************************************************************************************************************
void MeshRenderSystem::sortMeshes() // TODO: We can use here async bitonic sorting algorithm
{
	SET_CPU_ZONE_SCOPED("Meshes Sort");

	auto threadSystem = asyncPreparing ? ThreadSystem::Instance::tryGet() : nullptr;
	for (uint32 i = 0; i < unsortedBufferCount; i++)
	{
		auto unsortedBuffer = unsortedBuffers[i];
		if (unsortedBuffer->meshSystem->getMeshRenderType() == MeshRenderType::OIT ||
			unsortedBuffer->drawCount.load() == 0) // Note: No need to sort OIT meshes at all.
		{
			continue;
		}

		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([unsortedBuffer](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Unsorted Meshes Sort");
				auto& meshes = unsortedBuffer->combinedMeshes;
				std::sort(meshes.begin(), meshes.begin() + unsortedBuffer->drawCount.load());
			});
		}
		else
		{
			SET_CPU_ZONE_SCOPED("Unsorted Meshes Sort");
			auto& meshes = unsortedBuffer->combinedMeshes;
			std::sort(meshes.begin(), meshes.begin() + unsortedBuffer->drawCount.load());
		}
	}

	if (sortedDrawIndex.load() > 0)
	{
		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([this](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Sorted Meshes Sort");
				std::sort(sortedCombinedMeshes.begin(), sortedCombinedMeshes.begin() + sortedDrawIndex.load());
			});
		}
		else
		{
			SET_CPU_ZONE_SCOPED("Sorted Meshes Sort");
			std::sort(sortedCombinedMeshes.begin(), sortedCombinedMeshes.begin() + sortedDrawIndex.load());
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareMeshes(const f32x4x4& viewProj, 
	f32x4 cameraOffset, uint8 frustumPlaneCount, int8 shadowPass)
{
	SET_CPU_ZONE_SCOPED("Meshes Prepare");

	uint32 sortedMeshMaxCount = 0;
	sortedDrawIndex.store(0);
	unsortedBufferCount = sortedBufferCount = 0;
	hasAnyRefr = hasAnyOIT = hasAnyTransDepth = false;

	for (auto meshSystem : meshSystems)
	{
		auto renderType = meshSystem->getMeshRenderType();

		#if GARDEN_DEBUG
		if (hasOIT)
		{
			GARDEN_ASSERT_MSG(renderType != MeshRenderType::Translucent, 
				"Supported only translucent or OIT meshes, not both.");
		}
		else
		{
			GARDEN_ASSERT_MSG(renderType != MeshRenderType::OIT, 
				"Supported only translucent or OIT meshes, not both.");
		}
		#endif

		if (renderType == MeshRenderType::Translucent)
		{
			const auto& componentPool = meshSystem->getMeshComponentPool();
			sortedMeshMaxCount += componentPool.getCount();
			sortedBufferCount++;
			
		}
		else
		{
			unsortedBufferCount++;
		}
	}

	if (unsortedBuffers.size() < unsortedBufferCount)
	{
		auto i = (uint32)unsortedBuffers.size();
		unsortedBuffers.resize(unsortedBufferCount);
		for (; i < unsortedBufferCount; i++)
			unsortedBuffers[i] = new UnsortedBuffer();
	}
	if (sortedBuffers.size() < sortedBufferCount)
	{
		auto i = (uint32)sortedBuffers.size();
		sortedBuffers.resize(sortedBufferCount);
		for (; i < sortedBufferCount; i++)
			sortedBuffers[i] = new SortedBuffer();
	}

	if (sortedCombinedMeshes.size() < sortedMeshMaxCount)
		sortedCombinedMeshes.resize(sortedMeshMaxCount);

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto threadSystem = asyncPreparing ? ThreadSystem::Instance::tryGet() : nullptr;
	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	auto cameraPosition = cameraConstants.cameraPos;
	uint32 unsortedBufferIndex = 0, sortedBufferIndex = 0;
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
		
		if (renderType == MeshRenderType::Translucent)
		{
			auto bufferIndex = sortedBufferIndex++;
			auto sortedBuffer = sortedBuffers[bufferIndex];
			sortedBuffer->meshSystem = meshSystem;
			sortedBuffer->drawCount.store(0);

			if (componentCount == 0 || !meshSystem->isDrawReady(shadowPass))
				continue;

			if (threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				if (sortedThreadMeshes.size() < threadPool.getThreadCount())
					sortedThreadMeshes.resize(threadPool.getThreadCount());

				threadPool.addItems([&](const ThreadPool::Task& task)
				{
					prepareSortedMeshes(cameraOffset, cameraPosition, frustumPlanes, sortedBuffer, 
						sortedCombinedMeshes.data(), sortedThreadMeshes, sortedDrawIndex, bufferIndex, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), shadowPass, true);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareSortedMeshes(cameraOffset, cameraPosition, frustumPlanes, sortedBuffer, 
					sortedCombinedMeshes.data(), sortedThreadMeshes, sortedDrawIndex, bufferIndex, 
					0, componentPool.getOccupancy(), 0, shadowPass, false);
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->translucentTotalCount += componentCount;
			#endif	
		}
		else
		{
			auto unsortedBuffer = unsortedBuffers[unsortedBufferIndex++];
			unsortedBuffer->meshSystem = meshSystem;
			unsortedBuffer->drawCount.store(0);

			if (componentCount == 0 || !meshSystem->isDrawReady(shadowPass))
				continue;

			if (unsortedBuffer->combinedMeshes.size() < componentCount)
				unsortedBuffer->combinedMeshes.resize(componentCount);

			hasAnyRefr |= renderType == MeshRenderType::Refracted;
			hasAnyOIT |= renderType == MeshRenderType::OIT;
			hasAnyTransDepth |= renderType == MeshRenderType::TransDepth;
			
			if (threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				if (unsortedBuffer->threadMeshes.size() < threadPool.getThreadCount())
					unsortedBuffer->threadMeshes.resize(threadPool.getThreadCount());

				threadPool.addItems([=](const ThreadPool::Task& task)
				{
					prepareUnsortedMeshes(cameraOffset, cameraPosition, frustumPlanes, unsortedBuffer, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), shadowPass, true);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareUnsortedMeshes(cameraOffset, cameraPosition, frustumPlanes,
					unsortedBuffer, 0, componentPool.getOccupancy(), 0, shadowPass, false);
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
			{
				if (renderType == MeshRenderType::OIT || renderType == MeshRenderType::TransDepth)
					graphicsEditorSystem->translucentTotalCount += componentCount;
				else
					graphicsEditorSystem->opaqueTotalCount += componentCount;
			}
			#endif	
		}
	}		

	if (!meshSystems.empty())
	{
		if (threadSystem)
			threadSystem->getForegroundPool().wait();

		#if GARDEN_EDITOR
		if (graphicsEditorSystem)
		{
			for (uint32 i = 0; i < unsortedBufferCount; i++)
			{
				auto unsortedBuffer = unsortedBuffers[i];
				auto renderType = unsortedBuffer->meshSystem->getMeshRenderType();
				if (renderType == MeshRenderType::OIT || renderType == MeshRenderType::TransDepth)
					graphicsEditorSystem->translucentDrawCount += unsortedBuffer->drawCount.load();
				else
					graphicsEditorSystem->opaqueDrawCount += unsortedBuffer->drawCount.load();
			}

			graphicsEditorSystem->translucentDrawCount += sortedDrawIndex.load();
		}
		#endif	

		sortMeshes();

		if (threadSystem)
			threadSystem->getForegroundPool().wait();
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderUnsorted(const f32x4x4& viewProj, MeshRenderType renderType, int8 shadowPass)
{
	SET_CPU_ZONE_SCOPED("Unsorted Mesh Render");

	auto threadSystem = asyncRecording ? ThreadSystem::Instance::tryGet() : nullptr;
	for (uint32 i = 0; i < unsortedBufferCount; i++)
	{
		auto unsortedBuffer = unsortedBuffers[i];
		auto meshSystem = unsortedBuffer->meshSystem;
		auto drawCount = unsortedBuffer->drawCount.load();
		if (drawCount == 0 || meshSystem->getMeshRenderType() != renderType)
			continue;

		meshSystem->prepareDraw(viewProj, drawCount, shadowPass);

		if (threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems([unsortedBuffer, &viewProj](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Unsorted Mesh Draw");

				auto meshSystem = unsortedBuffer->meshSystem;
				const auto& meshes = unsortedBuffer->combinedMeshes;
				auto itemCount = task.getItemCount();
				auto taskIndex = task.getTaskIndex(); // Note: Using task index to preserve items order.
				auto taskCount = itemCount - task.getItemOffset();

				meshSystem->beginDrawAsync(taskIndex);
				for (uint32 j = task.getItemOffset(); j < itemCount; j++)
				{
					const auto& mesh = meshes[j];
					auto model = f32x4x4(mesh.model, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
					meshSystem->drawAsync(mesh.renderView, viewProj, model, j, taskIndex);
				}
				meshSystem->endDrawAsync(taskCount, taskIndex);
			},
			drawCount);
			threadPool.wait(); // Note: Required
		}
		else
		{
			SET_CPU_ZONE_SCOPED("Unsorted Mesh Draw");

			const auto& meshes = unsortedBuffer->combinedMeshes;
			meshSystem->beginDrawAsync(-1);
			for (uint32 j = 0; j < drawCount; j++)
			{
				const auto& mesh = meshes[j];
				auto model = f32x4x4(mesh.model, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
				meshSystem->drawAsync(mesh.renderView, viewProj, model, j, -1);
			}
			meshSystem->endDrawAsync(drawCount, -1);
		}

		meshSystem->finalizeDraw(viewProj, drawCount, shadowPass);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderSorted(const f32x4x4& viewProj, int8 shadowPass)
{
	SET_CPU_ZONE_SCOPED("Sorted Mesh Render");

	auto drawCount = sortedDrawIndex.load();
	if (drawCount == 0)
		return;

	for (uint32 i = 0; i < sortedBufferCount; i++)
	{
		auto sortedBuffer = sortedBuffers[i];
		auto bufferDrawCount = sortedBuffer->drawCount.load();
		if (bufferDrawCount == 0)
			continue;

		auto meshSystem = sortedBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, bufferDrawCount, shadowPass);
		sortedBuffer->drawCount.store(0);
	}

	auto threadSystem = asyncRecording ? ThreadSystem::Instance::tryGet() : nullptr;
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([this, &viewProj](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Sorted Mesh Draw");

			auto currentBufferIndex = sortedCombinedMeshes[task.getItemOffset()].bufferIndex;
			auto meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
			auto bufferDrawCount = &sortedBuffers[currentBufferIndex]->drawCount;
			auto itemCount = task.getItemCount();
			auto taskIndex = task.getTaskIndex(); // Note: Using task index to preserve items order.
			meshSystem->beginDrawAsync(taskIndex);

			uint32 currentDrawCount = 0;
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				const auto& mesh = sortedCombinedMeshes[i];
				if (currentBufferIndex != mesh.bufferIndex)
				{
					meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
					meshSystem->endDrawAsync(currentDrawCount, taskIndex);

					currentBufferIndex = mesh.bufferIndex;
					currentDrawCount = 0;

					bufferDrawCount = &sortedBuffers[currentBufferIndex]->drawCount;
					meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
					meshSystem->beginDrawAsync(taskIndex);
				}

				auto drawIndex = bufferDrawCount->fetch_add(1);
				auto model = f32x4x4(mesh.model, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
				meshSystem->drawAsync(mesh.renderView, viewProj, model, drawIndex, taskIndex);
				currentDrawCount++;
			}

			meshSystem->endDrawAsync(currentDrawCount, taskIndex);
		},
		drawCount);
		threadPool.wait(); // Note: Required
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Sorted Mesh Draw");

		auto currentBufferIndex = sortedCombinedMeshes[0].bufferIndex;
		auto meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
		auto bufferDrawCount = &sortedBuffers[currentBufferIndex]->drawCount;
		meshSystem->beginDrawAsync(-1);

		uint32 currentDrawCount = 0;
		for (uint32 i = 0; i < drawCount; i++)
		{
			const auto& mesh = sortedCombinedMeshes[i];
			if (currentBufferIndex != mesh.bufferIndex)
			{
				meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
				meshSystem->endDrawAsync(currentDrawCount, -1);

				currentBufferIndex = mesh.bufferIndex;
				currentDrawCount = 0;

				bufferDrawCount = &sortedBuffers[currentBufferIndex]->drawCount;
				meshSystem = sortedBuffers[currentBufferIndex]->meshSystem;
				meshSystem->beginDrawAsync(-1);
			}

			auto drawIndex = bufferDrawCount->fetch_add(1);
			auto model = f32x4x4(mesh.model, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
			meshSystem->drawAsync(mesh.renderView, viewProj, model, drawIndex, -1);
			currentDrawCount++;
		}

		meshSystem->endDrawAsync(currentDrawCount, -1);
	}

	for (uint32 i = 0; i < sortedBufferCount; i++)
	{
		auto sortedBuffer = sortedBuffers[i];
		auto drawCount = sortedBuffer->drawCount.load();
		if (drawCount == 0)
			continue;

		auto meshSystem = sortedBuffer->meshSystem;
		meshSystem->finalizeDraw(viewProj, drawCount, shadowPass);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::cleanupMeshes()
{
	for (uint32 i = 0; i < unsortedBufferCount; i++)
	{
		auto unsortedBuffer = unsortedBuffers[i];
		if (unsortedBuffer->drawCount.load() == 0)
			continue;
		unsortedBuffer->meshSystem->renderCleanup();
	}

	if (sortedDrawIndex.load() > 0)
	{
		for (uint32 i = 0; i < sortedBufferCount; i++)
		{
			auto sortedBuffer = sortedBuffers[i];
			if (sortedBuffer->drawCount.load() == 0)
				continue;
			sortedBuffer->meshSystem->renderCleanup();
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderShadows()
{
	SET_CPU_ZONE_SCOPED("Shadows Mesh Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);
	}
	graphicsSystem->stopRecording();

	const auto& systems = Manager::Instance::get()->getSystems();
	for (const auto& pair : systems)
	{
		auto shadowSystem = dynamic_cast<IShadowMeshRenderSystem*>(pair.second);
		if (!shadowSystem)
			continue;

		auto passCount = shadowSystem->getShadowPassCount();
		for (uint8 i = 0; i < passCount; i++)
		{
			f32x4x4 viewProj; f32x4 cameraOffset;
			if (!shadowSystem->prepareShadowRender(i, viewProj, cameraOffset))
				continue;

			prepareMeshes(viewProj, cameraOffset, Plane::frustumCount, i);

			graphicsSystem->startRecording(CommandBufferType::Frame);
			if (shadowSystem->beginShadowRender(i, MeshRenderType::Opaque))
			{
				SET_CPU_ZONE_SCOPED("Opaque/Color Shadow Render");
				SET_GPU_DEBUG_LABEL("Opaque/Color Shadow Pass", Color::transparent);
				renderUnsorted(viewProj, MeshRenderType::Opaque, i);
				renderUnsorted(viewProj, MeshRenderType::Color, i);
				// Note: No TransDepth rendering for shadows, expected RT instead.
				shadowSystem->endShadowRender(i, MeshRenderType::Opaque);
			}
			if (!isOpaqueOnly && shadowSystem->beginShadowRender(i, MeshRenderType::Translucent))
			{
				SET_CPU_ZONE_SCOPED("Translucent/Refracted/OIT Shadow Render");
				SET_GPU_DEBUG_LABEL("Translucent/Refracted/OIT Shadow Pass", Color::transparent);
				renderUnsorted(viewProj, MeshRenderType::Refracted, i);
				renderUnsorted(viewProj, MeshRenderType::OIT, i);
				renderSorted(viewProj, i);
				shadowSystem->endShadowRender(i, MeshRenderType::Translucent);
			}
			graphicsSystem->stopRecording();
		}

		cleanupMeshes();
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

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	prepareMeshes(cameraConstants.viewProj, f32x4::zero, Plane::frustumCount - 2, -1);
}

void MeshRenderSystem::forwardRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Forward Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::Opaque, -1);
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::Color, -1);

	if (!isOpaqueOnly)
	{
		renderUnsorted(cameraConstants.viewProj, MeshRenderType::Refracted, -1);
		renderUnsorted(cameraConstants.viewProj, MeshRenderType::OIT, -1);
		renderUnsorted(cameraConstants.viewProj, MeshRenderType::TransDepth, -1);
		renderSorted(cameraConstants.viewProj, -1);
	}
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

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	prepareMeshes(cameraConstants.viewProj, f32x4::zero, Plane::frustumCount - 2, -1);
}
void MeshRenderSystem::deferredRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Deferred Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::Opaque, -1);
}
void MeshRenderSystem::depthHdrRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Depth HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::Color, -1);
}
void MeshRenderSystem::preRefractedRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Refracted Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!hasAnyRefr || !graphicsSystem->camera)
		return;

	DeferredRenderSystem::Instance::get()->markAnyRefraction();
}
void MeshRenderSystem::refractedRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Refracted Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (isOpaqueOnly || !graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::Refracted, -1);
}
void MeshRenderSystem::translucentRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Translucent Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (isOpaqueOnly || !graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderSorted(cameraConstants.viewProj, -1);
}
void MeshRenderSystem::preTransDepthRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Translucent Depth Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!hasAnyTransDepth || !graphicsSystem->camera)
		return;

	DeferredRenderSystem::Instance::get()->markAnyTransDepth();
}
void MeshRenderSystem::transDepthRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Translucent Depth Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (isOpaqueOnly || !graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::TransDepth, -1);
}
void MeshRenderSystem::preOitRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre OIT Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!hasAnyOIT || !graphicsSystem->camera)
		return;

	DeferredRenderSystem::Instance::get()->markAnyOIT();
}
void MeshRenderSystem::oitRender()
{
	SET_CPU_ZONE_SCOPED("Mesh OIT Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (isOpaqueOnly || !graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	renderUnsorted(cameraConstants.viewProj, MeshRenderType::OIT, -1);
}