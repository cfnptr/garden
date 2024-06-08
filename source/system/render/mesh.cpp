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
#include "mpmt/atomic.h"

#if GARDEN_EDITOR
#include "garden/editor/system/graphics.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
MeshRenderSystem::MeshRenderSystem(bool useAsyncRecording)
{
	this->asyncRecording = useAsyncRecording;

	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", MeshRenderSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", MeshRenderSystem::deinit);
}
MeshRenderSystem::~MeshRenderSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", MeshRenderSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", MeshRenderSystem::deinit);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<ForwardRenderSystem>() || manager->has<DeferredRenderSystem>());

	if (manager->has<ForwardRenderSystem>())
	{
		SUBSCRIBE_TO_EVENT("PreForwardRender", MeshRenderSystem::preForwardRender);
		SUBSCRIBE_TO_EVENT("ForwardRender", MeshRenderSystem::forwardRender);
	}
	if (manager->has<DeferredRenderSystem>())
	{
		SUBSCRIBE_TO_EVENT("PreDeferredRender", MeshRenderSystem::preDeferredRender);
		SUBSCRIBE_TO_EVENT("DeferredRender", MeshRenderSystem::deferredRender);
		SUBSCRIBE_TO_EVENT("HdrRender", MeshRenderSystem::hdrRender);
	}
}
void MeshRenderSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		if (manager->has<ForwardRenderSystem>())
		{
			UNSUBSCRIBE_FROM_EVENT("PreForwardRender", MeshRenderSystem::preForwardRender);
			UNSUBSCRIBE_FROM_EVENT("ForwardRender", MeshRenderSystem::forwardRender);
		}
		if (manager->has<DeferredRenderSystem>())
		{
			UNSUBSCRIBE_FROM_EVENT("PreDeferredRender", MeshRenderSystem::preDeferredRender);
			UNSUBSCRIBE_FROM_EVENT("DeferredRender", MeshRenderSystem::deferredRender);
			UNSUBSCRIBE_FROM_EVENT("HdrRender", MeshRenderSystem::hdrRender);
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::prepareSystems()
{
	auto manager = Manager::getInstance();
	const auto& systems = manager->getSystems();
	meshSystems.clear();

	for (const auto& pair : systems)
	{
		auto meshSystem = dynamic_cast<IMeshRenderSystem*>(pair.second);
		if (meshSystem)
			meshSystems.push_back(meshSystem);
	}

	threadSystem = manager->tryGet<ThreadSystem>();

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
void MeshRenderSystem::prepareItems(const float4x4& viewProj, const float3& cameraOffset, 
	uint8 frustumPlaneCount, MeshRenderType opaqueType, MeshRenderType translucentType)
{
	uint32 translucentMaxCount = 0;
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
			auto& componentPool = meshSystem->getMeshComponentPool();
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
	translucentIndex = 0;

	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto graphicsEditorSystem = manager->tryGet<GraphicsEditorSystem>();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto& threadPool = threadSystem->getForegroundPool();	
	auto opaqueBufferData = opaqueBuffers.data();
	auto translucentBufferData = translucentBuffers.data();
	auto translucentItemData = translucentItems.data();
	auto translucentIndexData = translucentIndices.data();
	uint32 opaqueBufferIndex = 0, translucentBufferIndex = 0;

	Plane frustumPlanes[::frustumPlaneCount];
	extractFrustumPlanes(viewProj, frustumPlanes);

	// 1. Cull and prepare items 
	for (auto meshSystem : meshSystems)
	{
		auto& componentPool = meshSystem->getMeshComponentPool();
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
			
			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto meshSystem = opaqueBuffer->meshSystem;
				auto& componentPool = meshSystem->getMeshComponentPool();
				auto componentSize = meshSystem->getMeshComponentSize();
				auto componentData = (uint8*)componentPool.getData();
				auto itemCount = task.getItemCount();
				auto items = opaqueBuffer->items.data();
				auto indices = opaqueBuffer->indices.data();
				auto drawCount = &opaqueBuffer->drawCount;
				auto hasCameraOffset = cameraOffset != float3(0.0f);

				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					auto meshRender = (MeshRenderComponent*)(componentData + i * componentSize);
					if (!meshRender->entity || !meshRender->isEnabled)
						continue;

					float4x4 model;
					auto transform = manager->tryGet<TransformComponent>(meshRender->entity);
					if (transform)
					{
						model = transform->calcModel();
						setTranslation(model, getTranslation(model) - cameraPosition);
					}
					else
					{
						model = float4x4::identity;
					}

					if (isBehindFrustum(meshRender->aabb, model, frustumPlanes, frustumPlaneCount))
						continue;
					// TODO: optimize this using SpatialDB.
					// Or we can potentially extract BVH from the PhysX or Vulkan?
					// TODO: we can use full scene BVH to speed up frustum culling.
		
					RenderItem renderItem;
					renderItem.meshRender = meshRender;
					renderItem.model = model;
					renderItem.distance2 = hasCameraOffset ?
						length2(getTranslation(model) - cameraOffset) : length2(getTranslation(model));
					auto index = atomicFetchAdd64(drawCount, 1);
					items[index] = renderItem;
					indices[index] = index;
				}
			}),
			componentPool.getOccupancy());

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

			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto meshSystem = translucentBuffer->meshSystem;
				auto& componentPool = meshSystem->getMeshComponentPool();
				auto componentSize = meshSystem->getMeshComponentSize();
				auto componentData = (uint8*)componentPool.getData();
				auto itemCount = task.getItemCount();
				auto drawCount = &translucentBuffer->drawCount;
				auto hasCameraOffset = cameraOffset != float3(0.0f);
					
				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					auto meshRender = (MeshRenderComponent*)(componentData + i * componentSize);
					if (!meshRender->entity || !meshRender->isEnabled)
						continue;

					float4x4 model;
					auto transform = manager->tryGet<TransformComponent>(meshRender->entity);
					if (transform)
					{
						model = transform->calcModel();
						setTranslation(model, getTranslation(model) - cameraPosition);
					}
					else
					{
						model = float4x4::identity;
					}

					if (isBehindFrustum(meshRender->aabb, model, frustumPlanes, frustumPlaneCount))
						continue;
		
					TranslucentItem translucentItem;
					translucentItem.meshRender = meshRender;
					translucentItem.model = model;
					translucentItem.distance2 = hasCameraOffset ?
						length2(getTranslation(model) - cameraOffset) : length2(getTranslation(model));
					translucentItem.bufferIndex = bufferIndex;
					auto index = atomicFetchAdd64(&translucentIndex, 1);
					translucentItemData[index] = translucentItem;
					translucentIndexData[index] = index;
					atomicFetchAdd64(drawCount, 1);
				}
			}),
			componentPool.getOccupancy());

			#if GARDEN_EDITOR
			if (graphicsEditorSystem)
				graphicsEditorSystem->translucentTotalCount += componentCount;
			#endif	
		}
	}		

	threadPool.wait();

	// 2. Sort items with selected order
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBufferData[i];
		if (opaqueBuffer->drawCount == 0)
			continue;

		threadPool.addTask(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			auto items = opaqueBuffer->items.data();
			auto& indices = opaqueBuffer->indices;
			std::sort(indices.begin(), indices.begin() + 
				opaqueBuffer->drawCount, [&](uint32 a, uint32 b)
			{
				auto& ra = items[a]; auto& rb = items[b];
				return ra.distance2 < rb.distance2;
			});
		}));

		#if GARDEN_EDITOR
		if (graphicsEditorSystem)
			graphicsEditorSystem->opaqueDrawCount += (uint32)opaqueBuffer->drawCount;
		#endif	
	}

	if (translucentIndex > 0)
	{
		auto translucentItemData = translucentItems.data();
		threadPool.addTask(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			std::sort(translucentIndices.begin(), translucentIndices.begin() + 
				translucentIndex, [&](uint32 a, uint32 b)
			{
				auto& ra = translucentItemData[a]; auto& rb = translucentItemData[b];
				return ra.distance2 > rb.distance2;
			});
		}));

		#if GARDEN_EDITOR
		if (graphicsEditorSystem)
			graphicsEditorSystem->translucentDrawCount += (uint32)translucentIndex;
		#endif
	}

	threadPool.wait();
}

//**********************************************************************************************************************
void MeshRenderSystem::renderOpaque(const float4x4& viewProj)
{
	auto& threadPool = threadSystem->getForegroundPool();
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBuffers[i];
		auto drawCount = (uint32)opaqueBuffer->drawCount;
		if (drawCount == 0)
			continue;

		auto meshSystem = opaqueBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, drawCount);

		if (asyncRecording && threadSystem)
		{
			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto meshSystem = opaqueBuffer->meshSystem;
				auto itemCount = task.getItemCount();
				auto items = opaqueBuffer->items.data();
				auto indices = opaqueBuffer->indices.data();
				auto taskIndex = task.getTaskIndex();
				auto taskCount = itemCount - task.getItemOffset();

				meshSystem->beginDraw(taskIndex);
				for (uint32 j = task.getItemOffset(); j < itemCount; j++)
				{
					auto& item = items[indices[j]];
					meshSystem->draw(item.meshRender, viewProj, item.model, j, taskIndex);
				}
				meshSystem->endDraw(taskCount, taskIndex);
			}),
			drawCount);
			threadPool.wait(); // Required
		}
		else
		{
			auto items = opaqueBuffer->items.data();
			auto indices = opaqueBuffer->indices.data();

			meshSystem->beginDraw(-1);
			for (uint32 j = 0; j < drawCount; j++)
			{
				auto& item = items[indices[j]];
				meshSystem->draw(item.meshRender, viewProj, item.model, j, -1);
			}
			meshSystem->endDraw(drawCount, -1);
		}

		meshSystem->finalizeDraw(viewProj, drawCount);
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::renderTranslucent(const float4x4& viewProj)
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
	auto currentBufferIndex = items[indices[0]].bufferIndex;
	auto meshSystem = bufferData[currentBufferIndex].meshSystem;
	auto bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
	meshSystem->beginDraw(-1);

	// TODO: maybe we can somehow multithread this, without loosing ordering?
	// Opaque approach is not applicable here unfortunately.

	uint32 currentDrawCount = 0;
	for (uint32 i = 0; i < drawCount; i++)
	{
		auto& item = items[indices[i]];
		if (currentBufferIndex != item.bufferIndex)
		{
			meshSystem = bufferData[currentBufferIndex].meshSystem;
			meshSystem->endDraw(currentDrawCount, -1);

			currentBufferIndex = item.bufferIndex;
			currentDrawCount = 0;

			bufferDrawCount = &bufferData[currentBufferIndex].drawCount;
			meshSystem = bufferData[currentBufferIndex].meshSystem;
			meshSystem->beginDraw(-1);
		}

		auto drawIndex = (uint32)*bufferDrawCount;
		*bufferDrawCount = *bufferDrawCount + 1;
		meshSystem->draw(item.meshRender, viewProj, item.model, drawIndex, -1);
		currentDrawCount++;
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
void MeshRenderSystem::renderShadows()
{
	SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);

	const auto& systems = Manager::getInstance()->getSystems();
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

			prepareItems(viewProj, cameraOffset, frustumPlaneCount,
				MeshRenderType::OpaqueShadow, MeshRenderType::TranslucentShadow);

			shadowSystem->beginShadowRender(i, MeshRenderType::OpaqueShadow);
			renderOpaque(viewProj);
			shadowSystem->endShadowRender(i, MeshRenderType::OpaqueShadow);

			shadowSystem->beginShadowRender(i, MeshRenderType::TranslucentShadow);
			renderTranslucent(viewProj);
			shadowSystem->endShadowRender(i, MeshRenderType::TranslucentShadow);
		}
	}
}

//**********************************************************************************************************************
void MeshRenderSystem::preForwardRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	prepareSystems();
	renderShadows();

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareItems(cameraConstants.viewProj, float3(0.0f), frustumPlaneCount - 2,
		MeshRenderType::Opaque, MeshRenderType::Translucent);
}

void MeshRenderSystem::forwardRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(cameraConstants.viewProj);
	renderTranslucent(cameraConstants.viewProj);
}

//**********************************************************************************************************************
void MeshRenderSystem::preDeferredRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	prepareSystems();
	renderShadows();

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	prepareItems(cameraConstants.viewProj, float3(0.0f), frustumPlaneCount - 2,
		MeshRenderType::Opaque, MeshRenderType::Translucent);
}

void MeshRenderSystem::deferredRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderOpaque(cameraConstants.viewProj);
}

void MeshRenderSystem::hdrRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderTranslucent(cameraConstants.viewProj);
}