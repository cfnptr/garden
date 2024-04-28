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

#include "garden/editor/system/render/infinite-grid.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4x4 mvp;
	};
}

//**********************************************************************************************************************
InfiniteGridEditorSystem::InfiniteGridEditorSystem(Manager* manager) : System(manager)
{
	SUBSCRIBE_TO_EVENT("Init", InfiniteGridEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
}
InfiniteGridEditorSystem::~InfiniteGridEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", InfiniteGridEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", InfiniteGridEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::init()
{
	auto manager = getManager();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	SUBSCRIBE_TO_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto resourceSystem = ResourceSystem::getInstance();
	auto swapchainFramebuffer = graphicsSystem->getSwapchainFramebuffer();
	pipeline = resourceSystem->loadGraphicsPipeline("editor/infinite-grid", swapchainFramebuffer);
}
void InfiniteGridEditorSystem::deinit()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(pipeline);

		UNSUBSCRIBE_FROM_EVENT("EditorRender", InfiniteGridEditorSystem::editorRender);
	}
}

//**********************************************************************************************************************
void InfiniteGridEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->canRender() || !graphicsSystem->camera)
		return;
	
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());

	graphicsSystem->startRecording(CommandBufferType::Frame);
 	{
		SET_GPU_DEBUG_LABEL("Infinite Grid", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->drawFullscreen();
		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}
#endif