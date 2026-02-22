// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/ui/transform.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"
#include "math/matrix/projection.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/graphics.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
MeshRenderSystem::MeshRenderSystem(bool useOIT, bool useAsyncRecording, bool useAsyncPreparing, bool setSingleton) :
	Singleton(setSingleton), hasOIT(useOIT), asyncRecording(useAsyncRecording), asyncPreparing(useAsyncPreparing)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", MeshRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", MeshRenderSystem::deinit);
}
MeshRenderSystem::~MeshRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", MeshRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", MeshRenderSystem::deinit);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::init()
{
	auto manager = Manager::Instance::get();
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
		ECSM_SUBSCRIBE_TO_EVENT("PreRefrRender", MeshRenderSystem::preRefrRender);
		ECSM_SUBSCRIBE_TO_EVENT("RefractedRender", MeshRenderSystem::refractedRender);
		ECSM_SUBSCRIBE_TO_EVENT("PreTransDepthRender", MeshRenderSystem::preTransDepthRender);
		ECSM_SUBSCRIBE_TO_EVENT("TransDepthRender", MeshRenderSystem::transDepthRender);
		ECSM_SUBSCRIBE_TO_EVENT("UiRender", MeshRenderSystem::uiRender);

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
		auto manager = Manager::Instance::get();
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
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreRefrRender", MeshRenderSystem::preRefrRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("RefractedRender", MeshRenderSystem::refractedRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("PreTransDepthRender", MeshRenderSystem::preTransDepthRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("TransDepthRender", MeshRenderSystem::transDepthRender);
			ECSM_UNSUBSCRIBE_FROM_EVENT("UiRender", MeshRenderSystem::uiRender);

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
	meshSystems.clear();

	#if GARDEN_EDITOR
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	if (graphicsEditorSystem)
	{
		graphicsEditorSystem->opaqueDrawCount = graphicsEditorSystem->opaqueTotalCount =
			graphicsEditorSystem->translucentDrawCount = graphicsEditorSystem->translucentTotalCount = 0;
	}
	#endif

	auto systemGroup = manager->tryGetSystemGroup<IMeshRenderSystem>();
	if (!systemGroup)
		return;

	if (isNonTranslucent)
	{
		for (auto system : *systemGroup)
		{
			auto meshSystem = dynamic_cast<IMeshRenderSystem*>(system);
			auto renderType = meshSystem->getMeshRenderType();

			if (renderType == MeshRenderType::Color || renderType == MeshRenderType::Opaque || 
				renderType == MeshRenderType::UI)
			{
				meshSystems.push_back(meshSystem);
			}
		}
	}
	else
	{
		for (auto system : *systemGroup)
			meshSystems.push_back(dynamic_cast<IMeshRenderSystem*>(system));
	}
}

//**********************************************************************************************************************
static void prepareUnsortedMeshes(f32x4 cameraOffset, f32x4 cameraPosition, const Frustum& frustum, 
	MeshRenderSystem::UnsortedBuffer* unsortedBuffer, uint32 itemOffset, uint32 itemCount, uint32 threadIndex, 
	int8 shadowPass, bool useThreading)
{
	SET_CPU_ZONE_SCOPED("Unsorted Meshes Prepare");

	auto manager = Manager::Instance::get();
	auto meshSystem = unsortedBuffer->meshSystem;
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
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

	uint32 drawCount = 0, instanceCount = 0;
	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		auto aabbSize = meshRenderView->aabb.getSize(); aabbSize.fixW();

		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled || areAllTrue(aabbSize <= f32x4::zero))
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		auto transformView = manager->tryGet<TransformComponent>(meshRenderView->getEntity());
		if (!transformView || !transformView->isActive())
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		auto model = transformView->calcModel(cameraPosition); auto bakedModel = model;
		auto readyCount = meshSystem->getReadyMeshesAsync(meshRenderView, cameraPosition, frustum, bakedModel);
		if (readyCount == 0)
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		if (isNotShadowPass)
			meshRenderView->isVisible = true;

		MeshRenderSystem::UnsortedMesh unsortedMesh;
		unsortedMesh.componentOffset = i * componentSize;
		unsortedMesh.bakedModel = (float4x3)bakedModel;
		unsortedMesh.distanceSq = lengthSq3(getTranslation(model) + cameraOffset);
		meshes[drawCount++] = unsortedMesh;
		instanceCount += readyCount;
	}

	auto drawOffset = unsortedBuffer->drawCount.fetch_add(drawCount);
	unsortedBuffer->instanceCount.fetch_add(instanceCount);
	if (useThreading)
	{
		memcpy(unsortedBuffer->combinedMeshes.data() + drawOffset,
			meshes, drawCount * sizeof(MeshRenderSystem::UnsortedMesh));
	}
}

//**********************************************************************************************************************
static void prepareSortedMeshes(f32x4 cameraOffset, f32x4 cameraPosition, const Frustum& frustum, 
	MeshRenderSystem::SortedBuffer* sortedBuffer, MeshRenderSystem::SortedMesh* combinedMeshes, 
	vector<vector<MeshRenderSystem::SortedMesh>>& threadMeshes, atomic<uint32>* sortedDrawIndex, 
	uint32 bufferIndex, uint32 itemOffset, uint32 itemCount, uint32 threadIndex, int8 shadowPass, 
	bool useThreading, bool distance2D)
{
	SET_CPU_ZONE_SCOPED("Sorted Meshes Prepare");

	auto manager = Manager::Instance::get();
	auto meshSystem = sortedBuffer->meshSystem;
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
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

	uint32 drawCount = 0, instanceCount = 0;
	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		auto aabbSize = meshRenderView->aabb.getSize(); aabbSize.fixW();

		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled || areAllTrue(aabbSize <= f32x4::zero))
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		auto transformView = manager->tryGet<TransformComponent>(meshRenderView->getEntity());
		if (!transformView || !transformView->isActive())
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		auto model = transformView->calcModel(cameraPosition); auto bakedModel = model;
		auto readyCount = meshSystem->getReadyMeshesAsync(meshRenderView, cameraPosition, frustum, bakedModel);
		if (readyCount == 0)
		{
			if (isNotShadowPass)
				meshRenderView->isVisible = false;
			continue;
		}

		if (isNotShadowPass)
			meshRenderView->isVisible = true;

		MeshRenderSystem::SortedMesh sortedMesh;
		sortedMesh.componentOffset = i * componentSize;
		sortedMesh.bakedModel = (float4x3)bakedModel;
		sortedMesh.distanceSq = distance2D ? getTranslation(model).getZ() + 1.0f : 
			lengthSq3(getTranslation(model) + cameraOffset);
		sortedMesh.bufferIndex = bufferIndex;
		meshes[drawCount++] = sortedMesh;
		instanceCount += readyCount;
	}

	auto drawOffset = sortedDrawIndex->fetch_add(drawCount);
	if (useThreading)
		memcpy(combinedMeshes + drawOffset, meshes, drawCount * sizeof(MeshRenderSystem::SortedMesh));
	sortedBuffer->drawCount.fetch_add(drawCount);
	sortedBuffer->instanceCount.fetch_add(instanceCount);
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

	if (transDrawIndex.load() > 0)
	{
		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([this](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Trans Meshes Sort");
				std::sort(transSortedMeshes.begin(), transSortedMeshes.begin() + transDrawIndex.load());
			});
		}
		else
		{
			SET_CPU_ZONE_SCOPED("Trans Meshes Sort");
			std::sort(transSortedMeshes.begin(), transSortedMeshes.begin() + transDrawIndex.load());
		}
	}
	if (uiDrawIndex.load() > 0)
	{
		if (threadSystem)
		{
			threadSystem->getForegroundPool().addTask([this](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("UI Meshes Sort");
				std::sort(uiSortedMeshes.begin(), uiSortedMeshes.begin() + uiDrawIndex.load());
			});
		}
		else
		{
			SET_CPU_ZONE_SCOPED("UI Meshes Sort");
			std::sort(uiSortedMeshes.begin(), uiSortedMeshes.begin() + uiDrawIndex.load());
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareMeshes(const Frustum& viewFrustum, 
	const Frustum* uiFrustum, f32x4 cameraOffset, int8 shadowPass)
{
	SET_CPU_ZONE_SCOPED("Meshes Prepare");

	uint32 transMeshMaxCount = 0, uiMeshMaxCount = 0;
	transDrawIndex.store(0); uiDrawIndex.store(0);
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
			transMeshMaxCount += meshSystem->getMeshComponentPool().getCount();
			sortedBufferCount++;
		}
		else if (renderType == MeshRenderType::UI)
		{
			if (shadowPass < 0)
			{
				uiMeshMaxCount += meshSystem->getMeshComponentPool().getCount();
				sortedBufferCount++;
			}
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

	if (transSortedMeshes.size() < transMeshMaxCount)
		transSortedMeshes.resize(transMeshMaxCount);
	if (uiSortedMeshes.size() < uiMeshMaxCount)
		uiSortedMeshes.resize(uiMeshMaxCount);

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto threadSystem = asyncPreparing ? ThreadSystem::Instance::tryGet() : nullptr;
	const auto& cc = graphicsSystem->getCommonConstants();
	auto cameraPosition = (f32x4)cc.cameraPos;
	uint32 unsortedBufferIndex = 0, sortedBufferIndex = 0;

	#if GARDEN_EDITOR
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	#endif

	for (auto meshSystem : meshSystems)
	{
		const auto& componentPool = meshSystem->getMeshComponentPool();
		auto componentCount = componentPool.getCount();
		auto renderType = meshSystem->getMeshRenderType();
		
		if (renderType == MeshRenderType::Translucent || renderType == MeshRenderType::UI)
		{
			if (renderType == MeshRenderType::UI && shadowPass >= 0)
				continue;

			auto bufferIndex = sortedBufferIndex++;
			auto sortedBuffer = sortedBuffers[bufferIndex];
			sortedBuffer->meshSystem = meshSystem;
			sortedBuffer->drawCount.store(0);
			sortedBuffer->instanceCount.store(0);
			// Note: Still setting buffer system to reuse last mem allocation sizes.

			if (componentCount == 0 || !meshSystem->isDrawReady(shadowPass))
				continue;

			SortedMesh* combinedSortedMeshes; atomic<uint32>* sortedDrawIndex;
			const Frustum* frustum; f32x4 sortedCameraPos; bool distance2D;
			if (renderType == MeshRenderType::Translucent)
			{
				combinedSortedMeshes = transSortedMeshes.data();
				sortedDrawIndex = &transDrawIndex; frustum = &viewFrustum;
				sortedCameraPos = cameraPosition; distance2D = false;
			}
			else
			{
				combinedSortedMeshes = uiSortedMeshes.data();
				sortedDrawIndex = &uiDrawIndex; frustum = uiFrustum;
				sortedCameraPos = f32x4::zero; distance2D = true;
			}

			if (threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				if (sortedThreadMeshes.size() < threadPool.getThreadCount())
					sortedThreadMeshes.resize(threadPool.getThreadCount());

				// Note: do not optimize args with [&], it captures stack address!!!
				threadPool.addItems([this, cameraOffset, sortedCameraPos, frustum, 
					sortedBuffer, combinedSortedMeshes, sortedDrawIndex, bufferIndex, 
					shadowPass, distance2D](const ThreadPool::Task& task)
				{
					prepareSortedMeshes(cameraOffset, sortedCameraPos, *frustum, sortedBuffer, 
						combinedSortedMeshes, sortedThreadMeshes, sortedDrawIndex, bufferIndex, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), shadowPass, 
						true, distance2D);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareSortedMeshes(cameraOffset, sortedCameraPos, *frustum, sortedBuffer, 
					combinedSortedMeshes, sortedThreadMeshes, sortedDrawIndex, bufferIndex, 
					0, componentPool.getOccupancy(), 0, shadowPass, false, distance2D);
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
			unsortedBuffer->instanceCount.store(0);
			// Note: Still setting buffer system to reuse last mem allocation sizes.

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
					prepareUnsortedMeshes(cameraOffset, cameraPosition, viewFrustum, unsortedBuffer, 
						task.getItemOffset(), task.getItemCount(), task.getThreadIndex(), shadowPass, true);
				},
				componentPool.getOccupancy());
			}
			else
			{
				prepareUnsortedMeshes(cameraOffset, cameraPosition, viewFrustum,
					unsortedBuffer, 0, componentPool.getOccupancy(), 0, shadowPass, false);
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
			{
				if (renderType == MeshRenderType::OIT || renderType == 
					MeshRenderType::Refracted || renderType == MeshRenderType::TransDepth)
				{
					graphicsEditorSystem->translucentTotalCount += componentCount;
				}
				else graphicsEditorSystem->opaqueTotalCount += componentCount;
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
				if (renderType == MeshRenderType::OIT || renderType == 
					MeshRenderType::Refracted || renderType == MeshRenderType::TransDepth)
				{
					graphicsEditorSystem->translucentDrawCount += unsortedBuffer->drawCount.load();
				}
				else graphicsEditorSystem->opaqueDrawCount += unsortedBuffer->drawCount.load();
			}
			graphicsEditorSystem->translucentDrawCount += transDrawIndex.load() + uiDrawIndex.load();
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
	for (uint32 bufferIndex = 0; bufferIndex < unsortedBufferCount; bufferIndex++)
	{
		auto unsortedBuffer = unsortedBuffers[bufferIndex];
		auto meshSystem = unsortedBuffer->meshSystem;
		auto drawCount = unsortedBuffer->drawCount.load();
		if (drawCount == 0 || meshSystem->getMeshRenderType() != renderType)
			continue;

		auto& instanceCount = unsortedBuffer->instanceCount;
		meshSystem->prepareDraw(viewProj, drawCount, instanceCount.load(), shadowPass);
		auto totalInstanceCount = instanceCount.load(); instanceCount.store(0);

		if (threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems([unsortedBuffer, &viewProj, &instanceCount](const ThreadPool::Task& task)
			{
				SET_CPU_ZONE_SCOPED("Unsorted Mesh Draw");

				auto meshSystem = unsortedBuffer->meshSystem;
				const auto meshes = unsortedBuffer->combinedMeshes.data();
				auto componentSize = meshSystem->getMeshComponentSize();
				auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
				// Note: Using task index instead of thread to preserve items order.
				auto itemCount = task.getItemCount(), taskIndex = task.getTaskIndex();
				uint32 drawInstanceCount = 0;

				meshSystem->beginDrawAsync(taskIndex);
				for (uint32 drawIndex = task.getItemOffset(); drawIndex < itemCount; drawIndex++)
				{
					const auto& mesh = meshes[drawIndex];
					auto meshRenderView = (MeshRenderComponent*)(componentData + mesh.componentOffset);
					if (!meshRenderView->getEntity())
						continue; // Note: Entity may be destroyed before rendering.

					auto model = f32x4x4(mesh.bakedModel, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
					auto meshInstanceCount = meshSystem->getInstancesAsync(meshRenderView);
					auto instanceIndex = instanceCount.fetch_add(meshInstanceCount);
					meshSystem->drawAsync(meshRenderView, viewProj, model, instanceIndex, taskIndex);
					drawInstanceCount += meshInstanceCount;
				}
				meshSystem->endDrawAsync(drawInstanceCount, taskIndex);
			},
			drawCount);
			threadPool.wait(); // Note: Required.
		}
		else
		{
			SET_CPU_ZONE_SCOPED("Unsorted Mesh Draw");

			const auto meshes = unsortedBuffer->combinedMeshes.data();
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
			uint32 drawInstanceCount = 0;

			meshSystem->beginDrawAsync(-1);
			for (uint32 drawIndex = 0; drawIndex < drawCount; drawIndex++)
			{
				const auto& mesh = meshes[drawIndex];
				auto meshRenderView = (MeshRenderComponent*)(componentData + mesh.componentOffset);
				if (!meshRenderView->getEntity())
					continue; // Note: Entity may be destroyed before rendering.

				auto model = f32x4x4(mesh.bakedModel, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
				auto meshInstanceCount = meshSystem->getInstancesAsync(meshRenderView);
				auto instanceIndex = instanceCount.fetch_add(meshInstanceCount);
				meshSystem->drawAsync(meshRenderView, viewProj, model, drawInstanceCount, -1);
				drawInstanceCount += meshInstanceCount;
			}
			meshSystem->endDrawAsync(drawInstanceCount, -1);
		}

		GARDEN_ASSERT(instanceCount.load() <= totalInstanceCount);
		meshSystem->finalizeDraw(instanceCount.load());
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderSorted(const f32x4x4& viewProj, MeshRenderType renderType, int8 shadowPass)
{
	SET_CPU_ZONE_SCOPED("Sorted Mesh Render");

	const SortedMesh* combinedSortedMeshes; uint32 totalDrawCount;
	if (renderType == MeshRenderType::Translucent)
	{
		combinedSortedMeshes = transSortedMeshes.data();
		totalDrawCount = transDrawIndex.load();
	}
	else if (renderType == MeshRenderType::UI)
	{
		combinedSortedMeshes = uiSortedMeshes.data();
		totalDrawCount = uiDrawIndex.load();
	}
	else abort();

	if (totalDrawCount == 0)
		return;

	for (uint32 bufferIndex = 0; bufferIndex < sortedBufferCount; bufferIndex++)
	{
		auto sortedBuffer = sortedBuffers[bufferIndex];
		auto meshSystem = sortedBuffer->meshSystem;
		auto drawCount = sortedBuffer->drawCount.load();
		if (drawCount == 0 || meshSystem->getMeshRenderType() != renderType)
			continue;

		auto instanceCount = sortedBuffer->instanceCount.load();
		meshSystem->prepareDraw(viewProj, drawCount, instanceCount, shadowPass);
		sortedBuffer->instanceCount.store(0); // Note: Reusing instanceCount for rendering.
	}

	auto threadSystem = asyncRecording ? ThreadSystem::Instance::tryGet() : nullptr;
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([this, &viewProj, combinedSortedMeshes](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Sorted Mesh Draw");

			auto bufferIndex = combinedSortedMeshes[task.getItemOffset()].bufferIndex;
			auto meshSystem = sortedBuffers[bufferIndex]->meshSystem;
			auto instanceCount = &sortedBuffers[bufferIndex]->instanceCount;
			auto componentSize = meshSystem->getMeshComponentSize();
			auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
			// Note: Using task index instead of thread to preserve items order.
			auto itemCount = task.getItemCount(), taskIndex = task.getTaskIndex();
			uint32 drawInstanceCount = 0;

			meshSystem->beginDrawAsync(taskIndex);
			for (uint32 drawIndex = task.getItemOffset(); drawIndex < itemCount; drawIndex++)
			{
				const auto& mesh = combinedSortedMeshes[drawIndex];
				if (bufferIndex != mesh.bufferIndex)
				{
					meshSystem = sortedBuffers[bufferIndex]->meshSystem;
					meshSystem->endDrawAsync(drawInstanceCount, taskIndex);
					bufferIndex = mesh.bufferIndex; drawInstanceCount = 0;

					meshSystem = sortedBuffers[bufferIndex]->meshSystem;
					instanceCount = &sortedBuffers[bufferIndex]->instanceCount;
					componentSize = meshSystem->getMeshComponentSize();
					componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
					meshSystem->beginDrawAsync(taskIndex);
				}

				auto meshRenderView = (MeshRenderComponent*)(componentData + mesh.componentOffset);
				if (!meshRenderView->getEntity())
					continue; // Note: Entity may be destroyed before rendering.

				auto meshInstanceCount = meshSystem->getInstancesAsync(meshRenderView);
				auto instanceIndex = instanceCount->fetch_add(meshInstanceCount);
				auto model = f32x4x4(mesh.bakedModel, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
				meshSystem->drawAsync(meshRenderView, viewProj, model, instanceIndex, taskIndex);
				drawInstanceCount += meshInstanceCount;
			}
			meshSystem->endDrawAsync(drawInstanceCount, taskIndex);
		},
		totalDrawCount);
		threadPool.wait(); // Note: Required.
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Sorted Mesh Draw");

		auto bufferIndex = combinedSortedMeshes[0].bufferIndex;
		auto meshSystem = sortedBuffers[bufferIndex]->meshSystem;
		auto instanceCount = &sortedBuffers[bufferIndex]->instanceCount;
		auto componentSize = meshSystem->getMeshComponentSize();
		auto componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
		uint32 drawInstanceCount = 0;

		meshSystem->beginDrawAsync(-1);
		for (uint32 drawIndex = 0; drawIndex < totalDrawCount; drawIndex++)
		{
			const auto& mesh = combinedSortedMeshes[drawIndex];
			if (bufferIndex != mesh.bufferIndex)
			{
				meshSystem = sortedBuffers[bufferIndex]->meshSystem;
				meshSystem->endDrawAsync(drawInstanceCount, -1);
				bufferIndex = mesh.bufferIndex; drawInstanceCount = 0;

				meshSystem = sortedBuffers[bufferIndex]->meshSystem;
				instanceCount = &sortedBuffers[bufferIndex]->instanceCount;
				componentSize = meshSystem->getMeshComponentSize();
				componentData = (uint8*)meshSystem->getMeshComponentPool().getData();
				meshSystem->beginDrawAsync(-1);
			}

			auto meshRenderView = (MeshRenderComponent*)(componentData + mesh.componentOffset);
			if (!meshRenderView->getEntity())
				continue; // Note: Entity may be destroyed before rendering.

			auto meshInstanceCount = meshSystem->getInstancesAsync(meshRenderView);
			auto instanceIndex = instanceCount->fetch_add(meshInstanceCount);
			auto model = f32x4x4(mesh.bakedModel, f32x4(0.0f, 0.0f, 0.0f, 1.0f));
			meshSystem->drawAsync(meshRenderView, viewProj, model, instanceIndex, -1);
			drawInstanceCount += meshInstanceCount;
		}
		meshSystem->endDrawAsync(drawInstanceCount, -1);
	}

	for (uint32 bufferIndex = 0; bufferIndex < sortedBufferCount; bufferIndex++)
	{
		auto sortedBuffer = sortedBuffers[bufferIndex];
		auto instanceCount = sortedBuffer->instanceCount.load();
		if (instanceCount == 0)
			continue;
		sortedBuffer->meshSystem->finalizeDraw(instanceCount);
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
	if (transDrawIndex.load() > 0 || uiDrawIndex.load() > 0)
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

	auto systemGroup = Manager::Instance::get()->tryGetSystemGroup<IShadowMeshRenderSystem>();
	if (!systemGroup)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Frame);
	BEGIN_GPU_DEBUG_LABEL("Shadow Pass");
	graphicsSystem->stopRecording();

	for (auto system : *systemGroup)
	{
		auto shadowSystem = dynamic_cast<IShadowMeshRenderSystem*>(system);
		auto passCount = shadowSystem->getShadowPassCount();

		for (uint8 passIndex = 0; passIndex < passCount; passIndex++)
		{
			f32x4x4 viewProj; f32x4 cameraOffset;
			if (!shadowSystem->prepareShadowRender(passIndex, viewProj, cameraOffset))
				continue;

			prepareMeshes(Frustum(viewProj), nullptr, cameraOffset, passIndex);

			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_CPU_ZONE_SCOPED("Opaque Shadow Render");
				SET_GPU_DEBUG_LABEL("Opaque Shadow Pass");

				if (shadowSystem->beginShadowRender(passIndex, MeshRenderType::Opaque))
				{
					renderUnsorted(viewProj, MeshRenderType::Opaque, passIndex);
					renderUnsorted(viewProj, MeshRenderType::Color, passIndex);
					// Note: No TransDepth rendering for shadows, expected RT instead.
					shadowSystem->endShadowRender(passIndex, MeshRenderType::Opaque);
				}
			}
			{
				SET_CPU_ZONE_SCOPED("Trans Shadow Render");
				SET_GPU_DEBUG_LABEL("Trans Shadow Pass");

				if (!isNonTranslucent && shadowSystem->beginShadowRender(passIndex, MeshRenderType::Translucent))
				{
					renderUnsorted(viewProj, MeshRenderType::Refracted, passIndex);
					renderUnsorted(viewProj, MeshRenderType::OIT, passIndex);
					renderSorted(viewProj, MeshRenderType::Translucent, passIndex);
					shadowSystem->endShadowRender(passIndex, MeshRenderType::Translucent);
				}
			}
			graphicsSystem->stopRecording();
		}

		cleanupMeshes();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	END_GPU_DEBUG_LABEL();
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
static f32x4x4 calcUiProjView() noexcept
{
	auto halfSize = float2(0.5f);
	auto uiTransformSystem = UiTransformSystem::Instance::tryGet();
	if (uiTransformSystem)
		halfSize *= uiTransformSystem->getUiSize();
	return (f32x4x4)calcOrthoProjRevZ(float2(-halfSize.x, halfSize.x), 
		float2(-halfSize.y, halfSize.y), float2(-1.0f, 1.0f));
}

void MeshRenderSystem::preForwardRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Forward Render");

	prepareSystems();
	renderShadows();

	auto uiFrustum = Frustum(calcUiProjView());
	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	prepareMeshes(Frustum(cc.viewProj), &uiFrustum, f32x4::zero, -1);
}

void MeshRenderSystem::forwardRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Forward Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& cc = graphicsSystem->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::Opaque, -1);
	renderUnsorted(cc.viewProj, MeshRenderType::Color, -1);

	if (!isNonTranslucent)
	{
		renderUnsorted(cc.viewProj, MeshRenderType::Refracted, -1);
		renderUnsorted(cc.viewProj, MeshRenderType::OIT, -1);
		renderUnsorted(cc.viewProj, MeshRenderType::TransDepth, -1);
		renderSorted(cc.viewProj, MeshRenderType::Translucent, -1);
	}

	renderSorted(calcUiProjView(), MeshRenderType::UI, -1);
}

//**********************************************************************************************************************
void MeshRenderSystem::preDeferredRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Deferred Render");

	prepareSystems();
	renderShadows();

	auto uiFrustum = Frustum(calcUiProjView());
	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	prepareMeshes(Frustum(cc.viewProj), &uiFrustum, f32x4::zero, -1);
}
void MeshRenderSystem::deferredRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Deferred Render");

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::Opaque, -1);
}
void MeshRenderSystem::depthHdrRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Depth HDR Render");

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::Color, -1);
}
void MeshRenderSystem::preRefrRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Refracted Render");

	if (!hasAnyRefr)
		return;
	DeferredRenderSystem::Instance::get()->markAnyRefraction();
}
void MeshRenderSystem::refractedRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Refracted Render");

	if (isNonTranslucent)
		return;

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::Refracted, -1);
}
void MeshRenderSystem::translucentRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Translucent Render");

	if (isNonTranslucent)
		return;

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderSorted(cc.viewProj, MeshRenderType::Translucent, -1);
}
void MeshRenderSystem::preTransDepthRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre Translucent Depth Render");

	if (!hasAnyTransDepth)
		return;
	DeferredRenderSystem::Instance::get()->markAnyTransDepth();
}
void MeshRenderSystem::transDepthRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Translucent Depth Render");

	if (isNonTranslucent)
		return;

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::TransDepth, -1);
}
void MeshRenderSystem::preOitRender()
{
	SET_CPU_ZONE_SCOPED("Mesh Pre OIT Render");

	if (!hasAnyOIT)
		return;
	DeferredRenderSystem::Instance::get()->markAnyOIT();
}
void MeshRenderSystem::oitRender()
{
	SET_CPU_ZONE_SCOPED("Mesh OIT Render");

	if (isNonTranslucent)
		return;

	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();
	renderUnsorted(cc.viewProj, MeshRenderType::OIT, -1);
}
void MeshRenderSystem::uiRender()
{
	SET_CPU_ZONE_SCOPED("Mesh UI Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	renderSorted(calcUiProjView(), MeshRenderType::UI, -1);
}

//**********************************************************************************************************************
ModelStoreSystem::ModelStoreSystem(bool setSingleton) : Singleton(setSingleton) { }
ModelStoreSystem::~ModelStoreSystem() { unsetSingleton(); }
string_view ModelStoreSystem::getComponentName() const { return "Model Store"; }