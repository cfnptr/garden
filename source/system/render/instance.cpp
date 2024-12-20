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
static void createInstanceBuffers(uint64 bufferSize, DescriptorSetBuffers& instanceBuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	instanceBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Storage, Buffer::Access::SequentialWrite,
			bufferSize, Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(buffer, "buffer.storage.instances" + to_string(i));
		instanceBuffers[i].resize(1); instanceBuffers[i][0] = buffer;
	}
}

//**********************************************************************************************************************
InstanceRenderSystem::InstanceRenderSystem(bool useBaseDS, bool useDefaultDS) : 
	useBaseDS(useBaseDS), useDefaultDS(useDefaultDS)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", InstanceRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", InstanceRenderSystem::deinit);
}
InstanceRenderSystem::~InstanceRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", InstanceRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", InstanceRenderSystem::deinit);
	}
}

void InstanceRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", InstanceRenderSystem::swapchainRecreate);

	if (!pipeline)
		pipeline = createPipeline();

	createInstanceBuffers(getInstanceDataSize() * 16, instanceBuffers);
}
void InstanceRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(defaultDescriptorSet);
		graphicsSystem->destroy(baseDescriptorSet);
		graphicsSystem->destroy(instanceBuffers);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InstanceRenderSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
bool InstanceRenderSystem::isDrawReady()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);

	if (!pipelineView->isReady())
		return false;

	if (!baseDescriptorSet && useBaseDS)
	{
		auto uniforms = getBaseUniforms();
		if (uniforms.empty())
			return false;

		baseDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet.instance.base");
	}
	if (!defaultDescriptorSet && useDefaultDS)
	{
		auto uniforms = getDefaultUniforms();
		if (uniforms.empty())
			return false;

		defaultDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms), 1);
		SET_RESOURCE_DEBUG_NAME(defaultDescriptorSet, "descriptorSet.instance.default");
	}
	
	return true;
}
void InstanceRenderSystem::prepareDraw(const float4x4& viewProj, uint32 drawCount)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (graphicsSystem->get(instanceBuffers[0][0])->getBinarySize() < drawCount * getInstanceDataSize())
	{
		graphicsSystem->destroy(instanceBuffers);
		createInstanceBuffers(drawCount * getInstanceDataSize(), instanceBuffers);

		graphicsSystem->destroy(baseDescriptorSet);
		auto uniforms = getBaseUniforms();
		baseDescriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet.instance.base");
	}

	swapchainIndex = graphicsSystem->getSwapchainIndex();
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
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto instanceBufferView = graphicsSystem->get(instanceBuffers[swapchainIndex][0]);
	instanceBufferView->flush(drawCount * getInstanceDataSize());
}

//**********************************************************************************************************************
void InstanceRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount)
	{
		auto bufferSize = graphicsSystem->get(instanceBuffers[0][0])->getBinarySize();
		graphicsSystem->destroy(instanceBuffers);
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
	range[index++] = DescriptorSet::Range(baseDescriptorSet, 1, swapchainIndex);
}

#if GARDEN_DEBUG
void InstanceRenderSystem::setInstancesBuffersName(const string& debugName)
{
	for (uint32 i = 0; i < (uint32)instanceBuffers.size(); i++)
	{
		const auto& buffers = instanceBuffers[i];
		for (uint32 j = 0; j < (uint32)buffers.size(); j++)
			SET_RESOURCE_DEBUG_NAME(buffers[j], "buffer.storage.instances" + to_string(i) + "." + debugName);
	}
}
#endif

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