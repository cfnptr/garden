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

#include "garden/system/render/mesh.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/render/deferred.hpp"
#include "math/matrix/transform.hpp"
#include "mpmt/atomic.h"

#if GARDEN_EDITOR
#include "garden/editor/system/graphics.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
MeshRenderSystem::MeshRenderSystem(bool useAsyncRecording, bool useAsyncPreparing) :
	asyncRecording(useAsyncRecording), asyncPreparing(useAsyncPreparing)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", MeshRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", MeshRenderSystem::deinit);
}
MeshRenderSystem::~MeshRenderSystem()
{
	if (Manager::Instance::get()->isRunning())
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
		ECSM_SUBSCRIBE_TO_EVENT("HdrRender", MeshRenderSystem::hdrRender);
	}
}
void MeshRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning())
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
			ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", MeshRenderSystem::hdrRender);
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
static void prepareOpaqueItems(const float3& cameraOffset, const float3& cameraPosition, const Plane* frustumPlanes,
	MeshRenderSystem::OpaqueBuffer* opaqueBuffer, uint32 itemOffset, uint32 itemCount)
{
	auto meshSystem = opaqueBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto items = opaqueBuffer->items.data();
	auto indices = opaqueBuffer->indices.data();
	auto drawCount = &opaqueBuffer->drawCount;
	auto hasCameraOffset = cameraOffset != float3(0.0f);
	auto transformSystem = TransformSystem::Instance::get();

	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
			continue;

		float4x4 model;
		auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActiveWithAncestors())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = float4x4::identity;
		}

		if (isBehindFrustum(meshRenderView->aabb, model, frustumPlanes, Plane::frustumCount))
			continue;

		// TODO: optimize this using SpatialDB.
		// Or we can potentially extract BVH from the PhysX or Vulkan?
		// TODO: we can use full scene BVH to speed up frustum culling.

		MeshRenderSystem::RenderItem renderItem;
		renderItem.meshRenderView = meshRenderView;
		renderItem.model = model;
		renderItem.distance2 = hasCameraOffset ?
			length2(getTranslation(model) - cameraOffset) : length2(getTranslation(model));
		auto index = atomicFetchAdd64(drawCount, 1);
		items[index] = renderItem;
		indices[index] = index;
	}
}

//**********************************************************************************************************************
static void prepareTranslucentItems(const float3& cameraOffset, const float3& cameraPosition,
	const Plane* frustumPlanes, MeshRenderSystem::TranslucentBuffer* translucentBuffer,
	volatile int64* translucentIndex, uint32 bufferIndex, MeshRenderSystem::TranslucentItem* translucentItemData,
	uint32* translucentIndexData, uint32 itemOffset, uint32 itemCount)
{
	auto meshSystem = translucentBuffer->meshSystem;
	auto& componentPool = meshSystem->getMeshComponentPool();
	auto componentSize = meshSystem->getMeshComponentSize();
	auto componentData = (uint8*)componentPool.getData();
	auto drawCount = &translucentBuffer->drawCount;
	auto hasCameraOffset = cameraOffset != float3(0.0f);
	auto transformSystem = TransformSystem::Instance::get();

	for (uint32 i = itemOffset; i < itemCount; i++)
	{
		auto meshRenderView = (MeshRenderComponent*)(componentData + i * componentSize);
		if (!meshRenderView->getEntity() || !meshRenderView->isEnabled)
			continue;

		float4x4 model;
		auto transformView = transformSystem->tryGetComponent(meshRenderView->getEntity());
		if (transformView)
		{
			if (!transformView->isActiveWithAncestors())
				continue;
			model = transformView->calcModel(cameraPosition);
		}
		else
		{
			model = float4x4::identity;
		}

		if (isBehindFrustum(meshRenderView->aabb, model, frustumPlanes, Plane::frustumCount))
			continue;

		MeshRenderSystem::TranslucentItem translucentItem;
		translucentItem.meshRenderView = meshRenderView;
		translucentItem.model = model;
		translucentItem.distance2 = hasCameraOffset ?
			length2(getTranslation(model) - cameraOffset) : length2(getTranslation(model));
		translucentItem.bufferIndex = bufferIndex;
		auto index = atomicFetchAdd64(translucentIndex, 1);
		translucentItemData[index] = translucentItem;
		translucentIndexData[index] = index;
		atomicFetchAdd64(drawCount, 1);
	}
}

//**********************************************************************************************************************
static void sortOpaqueIndices(const MeshRenderSystem::RenderItem* items, vector<uint32>& indices, uint32 drawCount)
{
	std::sort(indices.begin(), indices.begin() + drawCount, [items](uint32 a, uint32 b)
	{
		auto& ra = items[a]; auto& rb = items[b];
		return ra.distance2 < rb.distance2;
	});
}
static void sortTranslucenntIndices(const MeshRenderSystem::TranslucentItem* items, 
	vector<uint32>* indices, uint32 translucentIndex)
{
	std::sort(indices->begin(), indices->begin() + translucentIndex, [items](uint32 a, uint32 b)
	{
		auto& ra = items[a]; auto& rb = items[b];
		return ra.distance2 > rb.distance2;
	});
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareItems(ThreadSystem* threadSystem, const float4x4& viewProj, const float3& cameraOffset,
	uint8 frustumPlaneCount, MeshRenderType opaqueType, MeshRenderType translucentType)
{
	uint32 translucentMaxCount = 0;
	translucentIndex = 0;
	opaqueBufferCount = translucentBufferCount = 0;

	for (auto meshSystem : meshSystems)
	{
		auto renderType = meshSystem->getMeshRenderType();
		if (renderType == opaqueType)
		{
			opaqueBufferCount++;
		}
		else if (renderType == translucentType)
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
	if (translucentItems.size() < translucentMaxCount)
		translucentItems.resize(translucentMaxCount);
	if (translucentIndices.size() < translucentMaxCount)
		translucentIndices.resize(translucentMaxCount);

	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto opaqueBufferData = opaqueBuffers.data();
	auto translucentBufferData = translucentBuffers.data();
	auto translucentItemData = translucentItems.data();
	auto translucentIndexData = translucentIndices.data();
	uint32 opaqueBufferIndex = 0, translucentBufferIndex = 0;
	Plane frustumPlanes[Plane::frustumCount];
	extractFrustumPlanes(viewProj, frustumPlanes);
	auto isAsyncPreparing = asyncPreparing && threadSystem;

	#if GARDEN_EDITOR
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	#endif

	// 1. Cull and prepare items 
	for (auto meshSystem : meshSystems)
	{
		const auto& componentPool = meshSystem->getMeshComponentPool();
		auto componentCount = componentPool.getCount();
		auto renderType = meshSystem->getMeshRenderType();
		
		if (renderType == opaqueType)
		{
			auto opaqueBuffer = &opaqueBufferData[opaqueBufferIndex++];
			opaqueBuffer->meshSystem = meshSystem;
			opaqueBuffer->drawCount = 0;

			if (componentCount == 0 || !meshSystem->isDrawReady())
				continue;

			if (opaqueBuffer->items.size() < componentCount)
				opaqueBuffer->items.resize(componentCount);
			if (opaqueBuffer->indices.size() < componentCount)
				opaqueBuffer->indices.resize(componentCount);
			
			if (isAsyncPreparing)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				threadPool.addItems(ThreadPool::Task([&cameraOffset, &cameraPosition, 
					frustumPlanes, opaqueBuffer](const ThreadPool::Task& task) // Do not optimize args!
				{
					prepareOpaqueItems(cameraOffset, cameraPosition, frustumPlanes,
						opaqueBuffer, task.getItemOffset(), task.getItemCount());
				}),
				componentPool.getOccupancy());
			}
			else
			{
				prepareOpaqueItems(cameraOffset, cameraPosition, frustumPlanes,
					opaqueBuffer, 0, componentPool.getOccupancy());
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->opaqueTotalCount += componentCount;
			#endif	
		}
		else if (renderType == translucentType)
		{
			auto bufferIndex = translucentBufferIndex++;
			auto translucentBuffer = &translucentBufferData[bufferIndex];
			translucentBuffer->meshSystem = meshSystem;
			translucentBuffer->drawCount = 0;

			if (componentCount == 0 || !meshSystem->isDrawReady())
				continue;

			if (isAsyncPreparing)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				threadPool.addItems(ThreadPool::Task([this, &cameraOffset, &cameraPosition, 
					frustumPlanes, translucentBuffer, translucentItemData, translucentIndexData, 
					bufferIndex](const ThreadPool::Task& task) // Do not optimize args!
				{
					prepareTranslucentItems(cameraOffset, cameraPosition, frustumPlanes, 
						translucentBuffer, &translucentIndex, bufferIndex, translucentItemData,
						translucentIndexData, task.getItemOffset(), task.getItemCount());
				}),
				componentPool.getOccupancy());
			}
			else
			{
				prepareTranslucentItems(cameraOffset, cameraPosition, frustumPlanes,
					translucentBuffer, &translucentIndex, bufferIndex, translucentItemData,
					translucentIndexData, 0, componentPool.getOccupancy());
			}

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->translucentTotalCount += componentCount;
			#endif	
		}
	}		

	if (isAsyncPreparing)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.wait();
	}

	// 2. Sort items with selected order
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBufferData[i];
		if (opaqueBuffer->drawCount == 0)
			continue;

		if (isAsyncPreparing)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addTask(ThreadPool::Task([opaqueBuffer](const ThreadPool::Task& task) // Do not optimize args!
			{
				sortOpaqueIndices(opaqueBuffer->items.data(), opaqueBuffer->indices, opaqueBuffer->drawCount);
			}));
		}
		else
		{
			sortOpaqueIndices(opaqueBuffer->items.data(), opaqueBuffer->indices, opaqueBuffer->drawCount);
		}

		#if GARDEN_EDITOR
		if (graphicsEditorSystem)
			graphicsEditorSystem->opaqueDrawCount += (uint32)opaqueBuffer->drawCount;
		#endif	
	}

	if (translucentIndex > 0)
	{
		auto translucentItemData = translucentItems.data();
		auto translucentIndices = &this->translucentIndices;

		// TODO: We can use here async sorting algorythm
		if (isAsyncPreparing)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addTask(ThreadPool::Task([this, translucentItemData, 
				translucentIndices](const ThreadPool::Task& task) // Do not optimize args!
			{
				sortTranslucenntIndices(translucentItemData, translucentIndices, translucentIndex);
			}));
		}
		else
		{
			sortTranslucenntIndices(translucentItemData, translucentIndices, translucentIndex);
		}

		#if GARDEN_EDITOR
		if (graphicsEditorSystem)
			graphicsEditorSystem->translucentDrawCount += (uint32)translucentIndex;
		#endif
	}

	if (isAsyncPreparing)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.wait();
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderOpaque(ThreadSystem* threadSystem, const float4x4& viewProj)
{
	auto isAsyncRecording = asyncRecording && threadSystem;
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBuffers[i];
		auto drawCount = (uint32)opaqueBuffer->drawCount;
		if (drawCount == 0)
			continue;

		auto meshSystem = opaqueBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, drawCount);

		if (isAsyncRecording)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto meshSystem = opaqueBuffer->meshSystem;
				auto items = opaqueBuffer->items.data();
				auto indices = opaqueBuffer->indices.data();
				auto itemCount = task.getItemCount();
				auto taskIndex = task.getTaskIndex();
				auto taskCount = itemCount - task.getItemOffset();

				meshSystem->beginDrawAsync(taskIndex);
				for (uint32 j = task.getItemOffset(); j < itemCount; j++)
				{
					const auto& item = items[indices[j]];
					meshSystem->drawAsync(item.meshRenderView, viewProj, item.model, j, taskIndex);
				}
				meshSystem->endDrawAsync(taskCount, taskIndex);
			}),
			drawCount);
			threadPool.wait(); // Required
		}
		else
		{
			auto items = opaqueBuffer->items.data();
			auto indices = opaqueBuffer->indices.data();

			meshSystem->beginDrawAsync(-1);
			for (uint32 j = 0; j < drawCount; j++)
			{
				const auto& item = items[indices[j]];
				meshSystem->drawAsync(item.meshRenderView, viewProj, item.model, j, -1);
			}
			meshSystem->endDrawAsync(drawCount, -1);
		}

		meshSystem->finalizeDraw(viewProj, drawCount);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderTranslucent(ThreadSystem* threadSystem, const float4x4& viewProj)
{
	auto drawCount = (uint32)translucentIndex;
	if (drawCount == 0)
		return;

	for (uint32 i = 0; i < translucentBufferCount; i++)
	{
		auto translucentBuffer = &translucentBuffers[i];
		auto bufferDrawCount = (uint32)translucentBuffer->drawCount;

		if (bufferDrawCount == 0)
			continue;

		auto meshSystem = translucentBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, bufferDrawCount);
		translucentBuffer->drawCount = 0;
	}

	auto bufferData = translucentBuffers.data();
	auto items = translucentItems.data();
	auto indices = translucentIndices.data();

	if (asyncRecording && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			auto currentBufferIndex = items[indices[task.getItemOffset()]].bufferIndex;
			auto meshSystem = bufferData[currentBufferIndex].meshSystem;
			auto bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
			auto itemCount = task.getItemCount(), taskIndex = task.getTaskIndex();
			meshSystem->beginDrawAsync(taskIndex);

			uint32 currentDrawCount = 0;
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				const auto& item = items[indices[i]];
				if (currentBufferIndex != item.bufferIndex)
				{
					meshSystem = bufferData[currentBufferIndex].meshSystem;
					meshSystem->endDrawAsync(currentDrawCount, taskIndex);

					currentBufferIndex = item.bufferIndex;
					currentDrawCount = 0;

					bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
					meshSystem = bufferData[currentBufferIndex].meshSystem;
					meshSystem->beginDrawAsync(taskIndex);
				}

				auto drawIndex = (uint32)*bufferDrawCount;
				*bufferDrawCount = *bufferDrawCount + 1;
				meshSystem->drawAsync(item.meshRenderView, viewProj, item.model, drawIndex, taskIndex);
				currentDrawCount++;
			}

			meshSystem->endDrawAsync(currentDrawCount, taskIndex);
		}),
		drawCount);
		threadPool.wait(); // Required
	}
	else
	{
		auto currentBufferIndex = items[indices[0]].bufferIndex;
		auto meshSystem = bufferData[currentBufferIndex].meshSystem;
		auto bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
		meshSystem->beginDrawAsync(-1);

		uint32 currentDrawCount = 0;
		for (uint32 i = 0; i < drawCount; i++)
		{
			const auto& item = items[indices[i]];
			if (currentBufferIndex != item.bufferIndex)
			{
				meshSystem = bufferData[currentBufferIndex].meshSystem;
				meshSystem->endDrawAsync(currentDrawCount, -1);

				currentBufferIndex = item.bufferIndex;
				currentDrawCount = 0;

				bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
				meshSystem = bufferData[currentBufferIndex].meshSystem;
				meshSystem->beginDrawAsync(-1);
			}

			auto drawIndex = (uint32)*bufferDrawCount;
			*bufferDrawCount = *bufferDrawCount + 1;
			meshSystem->drawAsync(item.meshRenderView, viewProj, item.model, drawIndex, -1);
			currentDrawCount++;
		}

		meshSystem->endDrawAsync(currentDrawCount, -1);
	}

	for (uint32 i = 0; i < translucentBufferCount; i++)
	{
		auto translucentBuffer = &translucentBuffers[i];
		auto drawCount = (uint32)translucentBuffer->drawCount;
		if (drawCount == 0)
			continue;

		auto meshSystem = translucentBuffer->meshSystem;
		meshSystem->finalizeDraw(viewProj, drawCount);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderShadows(ThreadSystem* threadSystem)
{
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

			prepareItems(threadSystem, viewProj, cameraOffset, Plane::frustumCount,
				MeshRenderType::OpaqueShadow, MeshRenderType::TranslucentShadow);

			shadowSystem->beginShadowRender(i, MeshRenderType::OpaqueShadow);
			renderOpaque(threadSystem, viewProj);
			shadowSystem->endShadowRender(i, MeshRenderType::OpaqueShadow);

			shadowSystem->beginShadowRender(i, MeshRenderType::TranslucentShadow);
			renderTranslucent(threadSystem, viewProj);
			shadowSystem->endShadowRender(i, MeshRenderType::TranslucentShadow);
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::preForwardRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	prepareSystems();
	renderShadows(threadSystem);

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareItems(threadSystem, cameraConstants.viewProj, float3(0.0f), 
		Plane::frustumCount - 2, MeshRenderType::Opaque, MeshRenderType::Translucent);
}

void MeshRenderSystem::forwardRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(threadSystem, cameraConstants.viewProj);
	renderTranslucent(threadSystem, cameraConstants.viewProj);
}

//**********************************************************************************************************************
void MeshRenderSystem::preDeferredRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	prepareSystems();
	renderShadows(threadSystem);

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareItems(threadSystem, cameraConstants.viewProj, float3(0.0f),
		Plane::frustumCount - 2, MeshRenderType::Opaque, MeshRenderType::Translucent);
}

void MeshRenderSystem::deferredRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(threadSystem, cameraConstants.viewProj);
}

void MeshRenderSystem::hdrRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderTranslucent(threadSystem, cameraConstants.viewProj);
}