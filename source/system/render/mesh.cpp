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

/*
#include "garden/system/render/mesh.hpp"
#include "garden/system/render/editor/selector.hpp"
#include "garden/system/render/editor/gizmos.hpp"
#include "garden/system/render/editor.hpp"// TODO: remove this include
#include "garden/system/render/deferred.hpp"
#include "mpmt/atomic.h"

using namespace garden;

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::initialize()
{
	auto manager = getManager();
	transformSystem = manager->get<TransformSystem>();
	threadSystem = manager->get<ThreadSystem>();

	#if GARDEN_EDITOR
	selectorEditor = new SelectorEditor(this);
	gizmosEditor = new GizmosEditor(this);
	#endif
}
void MeshRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (GizmosEditor*)gizmosEditor;
	delete (SelectorEditor*)selectorEditor;
	#endif
}

// TODO: Refactor code. Make big functions smaller.
// TODO: We are iterating over shadow and non shadow subsystems. This is suboptimal.

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::prepareItems(const float4x4& viewProj, const float3& cameraPosition,
	const vector<Manager::SubsystemData>& subsystems, MeshRenderType opaqueType,
	MeshRenderType translucentType, uint8 frustumPlaneCount, const float3& cameraOffset)
{
	uint32 translucentMaxCount = 0;
	opaqueBufferCount = translucentBufferCount = 0;

	for (auto& subsystem : subsystems)
	{
		auto meshSystem = dynamic_cast<IMeshRenderSystem*>(subsystem.system);
		GARDEN_ASSERT(meshSystem);
		
		auto renderType = meshSystem->getMeshRenderType();
		if (renderType == opaqueType)
			opaqueBufferCount++;
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

	auto& transformData = transformSystem->getComponents();
	auto& threadPool = threadSystem->getForegroundPool();	
	auto opaqueBufferData = opaqueBuffers.data();
	auto translucentBufferData = translucentBuffers.data();
	auto translucentItemData = translucentItems.data();
	auto translucentIndexData = translucentIndices.data();
	uint32 opaqueBufferIndex = 0, translucentBufferIndex = 0;

	Plane frustumPlanes[FRUSTUM_PLANE_COUNT];
	extractFrustumPlanes(viewProj, frustumPlanes);

	#if GARDEN_EDITOR
	auto editorSystem = getManager()->get<EditorRenderSystem>();
	#endif

	// 1. Cull and prepare items 
	for (auto& subsystem : subsystems)
	{
		auto meshSystem = dynamic_cast<IMeshRenderSystem*>(subsystem.system);
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
				auto opaqueBuffer = (OpaqueBuffer*)task.getArgument();
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
					auto meshRender = (MeshRenderComponent*)(
						componentData + i * componentSize);
					if (!meshRender->entity || !meshRender->isEnabled)
						continue;

					auto transform = transformData.get(meshRender->transform);
					auto model = transform->calcModel();
					setTranslation(model, getTranslation(model) - cameraPosition);

					if (isBehindFrustum(meshRender->aabb, model, frustumPlanes, frustumPlaneCount))
						continue;
					// TODO: optimize this using SpatialDB.
					// Or we can potentially extract BVH from the PhysX or Vulkan?
					// TODO: we can use full scene BVH to speed up frustum culling.
		
					RenderItem renderItem;
					renderItem.meshRender = meshRender;
					renderItem.model = model;
					renderItem.distance2 = hasCameraOffset ?
						length2(getTranslation(model) - cameraOffset) :
						length2(getTranslation(model));
					auto index = atomicFetchAdd64(drawCount, 1);
					items[index] = renderItem;
					indices[index] = index;
				}
			},
			opaqueBuffer), componentPool.getOccupancy());

			#if GARDEN_EDITOR
			editorSystem->opaqueTotalCount += componentCount;
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
				auto translucentBuffer = (TranslucentBuffer*)task.getArgument();
				auto meshSystem = translucentBuffer->meshSystem;
				auto& componentPool = meshSystem->getMeshComponentPool();
				auto componentSize = meshSystem->getMeshComponentSize();
				auto componentData = (uint8*)componentPool.getData();
				auto itemCount = task.getItemCount();
				auto drawCount = &translucentBuffer->drawCount;
				auto hasCameraOffset = cameraOffset != float3(0.0f);
					
				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					auto meshRender = (MeshRenderComponent*)(
						componentData + i * componentSize);
					if (!meshRender->entity || !meshRender->isEnabled)
						continue;

					auto transform = transformData.get(meshRender->transform);
					auto model = transform->calcModel();
					setTranslation(model, getTranslation(model) - cameraPosition);

					if (isBehindFrustum(meshRender->aabb, model,
						frustumPlanes, frustumPlaneCount))
					{
						continue;
					}
		
					TranslucentItem translucentItem;
					translucentItem.meshRender = meshRender;
					translucentItem.model = model;
					translucentItem.distance2 = hasCameraOffset ?
						length2(getTranslation(model) - cameraOffset) :
						length2(getTranslation(model));
					translucentItem.bufferIndex = bufferIndex;
					auto index = atomicFetchAdd64(&translucentIndex, 1);
					translucentItemData[index] = translucentItem;
					translucentIndexData[index] = index;
					atomicFetchAdd64(drawCount, 1);
				}
			},
			translucentBuffer), componentPool.getOccupancy());

			#if GARDEN_EDITOR
			editorSystem->translucentTotalCount += componentCount;
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

		threadPool.addTask(ThreadPool::Task([](const ThreadPool::Task& task)
		{
			auto opaqueBuffer = (OpaqueBuffer*)task.getArgument();
			auto items = opaqueBuffer->items.data();
			auto& indices = opaqueBuffer->indices;
			std::sort(indices.begin(), indices.begin() +
				opaqueBuffer->drawCount, [&](uint32 a, uint32 b)
			{
				auto& ra = items[a]; auto& rb = items[b];
				return ra.distance2 < rb.distance2;
			});
		},
		opaqueBuffer));

		#if GARDEN_EDITOR
		editorSystem->opaqueDrawCount += (uint32)opaqueBuffer->drawCount;
		#endif	
	}

	if (translucentIndex > 0)
	{
		threadPool.addTask(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			auto items = (TranslucentItem*)task.getArgument();
			std::sort(translucentIndices.begin(), translucentIndices.begin() +
				translucentIndex, [&](uint32 a, uint32 b)
			{
				auto& ra = items[a]; auto& rb = items[b];
				return ra.distance2 > rb.distance2;
			});
		},
		translucentItems.data()));

		#if GARDEN_EDITOR
		editorSystem->translucentDrawCount += (uint32)translucentIndex;
		#endif
	}

	threadPool.wait();
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::renderOpaqueItems(
	const float4x4& viewProj, ID<Framebuffer> framebuffer)
{
	auto& threadPool = threadSystem->getForegroundPool();
	for (uint32 i = 0; i < opaqueBufferCount; i++)
	{
		auto opaqueBuffer = &opaqueBuffers[i];
		auto drawCount = (uint32)opaqueBuffer->drawCount;
		if (drawCount == 0)
			continue;

		auto meshSystem = opaqueBuffer->meshSystem;
		meshSystem->prepareDraw(viewProj, framebuffer, drawCount);

		if (isAsync)
		{
			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto opaqueBuffer = (OpaqueBuffer*)task.getArgument();
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
			},
			opaqueBuffer), drawCount);
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

		meshSystem->finalizeDraw(viewProj, framebuffer, drawCount);
	}
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::renderTranslucentItems(
	const float4x4& viewProj, ID<Framebuffer> framebuffer)
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
		meshSystem->prepareDraw(viewProj, framebuffer, bufferDrawCount);
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
	// Opaque approach is not applicable here unfortunatly.

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
		meshSystem->finalizeDraw(viewProj, framebuffer, drawCount);
	}
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::render()
{
	auto graphicsSystem = getGraphicsSystem();
	if (!graphicsSystem->camera)
		return;

	auto manager = getManager();
	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto cameraPosition = (float3)cameraConstants.cameraPos;
	auto& subsystems = manager->getSubsystems<MeshRenderSystem>();
	graphicsSystem->startRecording(CommandBufferType::Frame);

	#if GARDEN_EDITOR
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->opaqueDrawCount = editorSystem->opaqueTotalCount =
		editorSystem->translucentDrawCount = editorSystem->translucentTotalCount = 0;
	#endif

	{
		SET_GPU_DEBUG_LABEL("Shadow Buffering", Color::transparent);
		for (auto shadowSystem : shadowSystems)
		{
			auto passCount = shadowSystem->getShadowPassCount();

			for (uint32 i = 0; i < passCount; i++)
			{
				float4x4 viewProj; float3 cameraOffset; ID<Framebuffer> framebuffer;
				if (!shadowSystem->prepareShadowRender(i, viewProj, cameraOffset, framebuffer))
					continue;

				prepareItems(viewProj, cameraPosition, subsystems,
					MeshRenderType::OpaqueShadow, MeshRenderType::TranslucentShadow,
					FRUSTUM_PLANE_COUNT, cameraOffset);

				shadowSystem->beginShadowRender(i, MeshRenderType::OpaqueShadow);
				renderOpaqueItems(viewProj, framebuffer);
				shadowSystem->endShadowRender(i, MeshRenderType::OpaqueShadow);

				shadowSystem->beginShadowRender(i, MeshRenderType::TranslucentShadow);
				renderTranslucentItems(viewProj, framebuffer);
				shadowSystem->endShadowRender(i, MeshRenderType::TranslucentShadow);
			}
		}
	}

	graphicsSystem->stopRecording();
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::deferredRender()
{
	auto graphicsSystem = getGraphicsSystem();
	if (!graphicsSystem->camera)
		return;

	auto manager = getManager();
	auto& subsystems = manager->getSubsystems<MeshRenderSystem>();
	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	
	prepareItems(cameraConstants.viewProj, (float3)cameraConstants.cameraPos,
		subsystems, MeshRenderType::Opaque, MeshRenderType::Translucent,
		FRUSTUM_PLANE_COUNT - 2, float3(0.0f));
	renderOpaqueItems(cameraConstants.viewProj, getDeferredSystem()->getGFramebuffer());
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::hdrRender()
{
	auto graphicsSystem = getGraphicsSystem();
	if (!graphicsSystem->camera)
		return;

	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	renderTranslucentItems(cameraConstants.viewProj, getDeferredSystem()->getGFramebuffer());
}

//--------------------------------------------------------------------------------------------------
void MeshRenderSystem::preSwapchainRender()
{
	#if GARDEN_EDITOR
	((GizmosEditor*)gizmosEditor)->preSwapchainRender();
	((SelectorEditor*)selectorEditor)->preSwapchainRender();
	#endif
}
*/