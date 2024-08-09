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

#include "garden/system/render/instance.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
static void createInstanceBuffers(uint64 bufferSize, vector<vector<ID<Buffer>>>& instanceBuffers)
{
	auto graphicsSystem = GraphicsSystem::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	instanceBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Storage, Buffer::Access::SequentialWrite,
			bufferSize, Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.storage.instances" + to_string(i));
		instanceBuffers[i].push_back(buffer);
	}
}
static void destroyInstanceBuffers(vector<vector<ID<Buffer>>>& instanceBuffers)
{
	auto graphicsSystem = GraphicsSystem::get();
	for (const auto& sets : instanceBuffers)
		graphicsSystem->destroy(sets[0]);
	instanceBuffers.clear();
}

//**********************************************************************************************************************
InstanceRenderSystem::InstanceRenderSystem()
{
	SUBSCRIBE_TO_EVENT("Init", InstanceRenderSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", InstanceRenderSystem::deinit);
}
InstanceRenderSystem::~InstanceRenderSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", InstanceRenderSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", InstanceRenderSystem::deinit);
	}
}

void InstanceRenderSystem::init()
{
	SUBSCRIBE_TO_EVENT("SwapchainRecreate", InstanceRenderSystem::swapchainRecreate);

	if (!pipeline)
		pipeline = createPipeline();

	createInstanceBuffers(getInstanceDataSize() * 16, instanceBuffers);
}
void InstanceRenderSystem::deinit()
{
	if (Manager::get()->isRunning())
	{
		destroyInstanceBuffers(instanceBuffers);
		GraphicsSystem::get()->destroy(pipeline);

		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InstanceRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
bool InstanceRenderSystem::isDrawReady()
{
	auto graphicsSystem = GraphicsSystem::get();
	auto pipelineView = graphicsSystem->get(pipeline);

	if (!pipelineView->isReady())
		return false;

	if (!baseDescriptorSet)
	{
		auto uniforms = getBaseUniforms();
		baseDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, baseDescriptorSet, "descriptorSet.instance.base");
	}
	if (!defaultDescriptorSet)
	{
		auto uniforms = getDefaultUniforms(); 
		defaultDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms), 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, defaultDescriptorSet, "descriptorSet.instance.default");
	}
	
	return true;
}
void InstanceRenderSystem::prepareDraw(const float4x4& viewProj, uint32 drawCount)
{
	auto graphicsSystem = GraphicsSystem::get();
	if (graphicsSystem->get(instanceBuffers[0][0])->getBinarySize() < drawCount * getInstanceDataSize())
	{
		destroyInstanceBuffers(instanceBuffers);
		createInstanceBuffers(drawCount * getInstanceDataSize(), instanceBuffers);

		graphicsSystem->destroy(baseDescriptorSet);
		auto uniforms = getBaseUniforms();
		baseDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, baseDescriptorSet, "descriptorSet.instance.base");
	}

	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	auto instanceBufferView = graphicsSystem->get(instanceBuffers[swapchainIndex][0]);
	pipelineView = graphicsSystem->get(pipeline);
	instanceMap = instanceBufferView->getMap();
}
void InstanceRenderSystem::beginDrawAsync(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(float4(0.0f), taskIndex);
}
void InstanceRenderSystem::finalizeDraw(const float4x4& viewProj, uint32 drawCount)
{
	auto graphicsSystem = GraphicsSystem::get();
	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	auto instanceBufferView = graphicsSystem->get(instanceBuffers[swapchainIndex][0]);
	instanceBufferView->flush(drawCount * getInstanceDataSize());
}

//**********************************************************************************************************************
void InstanceRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount)
	{
		auto bufferSize = graphicsSystem->get(instanceBuffers[0][0])->getBinarySize();
		destroyInstanceBuffers(instanceBuffers);
		createInstanceBuffers(bufferSize, instanceBuffers);

		if (baseDescriptorSet)
		{
			auto uniforms = getBaseUniforms();
			auto descriptorSetView = graphicsSystem->get(baseDescriptorSet);
			descriptorSetView->recreate(std::move(uniforms));
		}
	}
}

//**********************************************************************************************************************
void InstanceRenderSystem::setDescriptorSetRange(MeshRenderComponent* meshRenderView,
	DescriptorSet::Range* range, uint8& index, uint8 capacity)
{
	GARDEN_ASSERT(index < capacity);
	auto swapchainIndex = GraphicsSystem::get()->getSwapchainIndex();
	range[index++] = DescriptorSet::Range(baseDescriptorSet, 1, swapchainIndex);
}

map<string, DescriptorSet::Uniform> InstanceRenderSystem::getBaseUniforms()
{
	map<string, DescriptorSet::Uniform> baseUniforms =
	{ { "instance", DescriptorSet::Uniform(instanceBuffers) } };
	return baseUniforms;
}

ID<GraphicsPipeline> InstanceRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}