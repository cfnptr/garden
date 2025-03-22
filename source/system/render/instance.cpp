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

#include "garden/system/render/instance.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
static void createInstanceBuffers(uint64 bufferSize, DescriptorSetBuffers& instanceBuffers, 
	bool isShadow, InstanceRenderSystem* system)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	instanceBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Storage, Buffer::Access::SequentialWrite,
			bufferSize, Buffer::Usage::Auto, Buffer::Strategy::Size);
		#if GARDEN_DEBUG
		if (isShadow)
		{
			SET_RESOURCE_DEBUG_NAME(buffer, "buffer.storage." + 
				system->debugResourceName + ".shadowInstances" + to_string(i));
		}
		else
		{
			SET_RESOURCE_DEBUG_NAME(buffer, "buffer.storage." + 
				system->debugResourceName + ".instances" + to_string(i));
		}
		#endif
		instanceBuffers[i].resize(1); instanceBuffers[i][0] = buffer;
	}
}

//**********************************************************************************************************************
InstanceRenderSystem::InstanceRenderSystem()
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
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", InstanceRenderSystem::gBufferRecreate);

	if (!basePipeline)
		basePipeline = createBasePipeline();
	if (!shadowPipeline)
		shadowPipeline = createShadowPipeline();

	createInstanceBuffers(getBaseInstanceDataSize() * 16, baseInstanceBuffers, false, this);
	if (shadowPipeline)
		createInstanceBuffers(getShadowInstanceDataSize() * 16, shadowInstanceBuffers, true, this);
}
void InstanceRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(shadowDescriptorSet);
		graphicsSystem->destroy(baseDescriptorSet);
		graphicsSystem->destroy(shadowInstanceBuffers);
		graphicsSystem->destroy(baseInstanceBuffers);
		graphicsSystem->destroy(shadowPipeline);
		graphicsSystem->destroy(basePipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", InstanceRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
bool InstanceRenderSystem::isDrawReady(bool isShadowPass)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (isShadowPass)
	{
		if (!shadowPipeline || !graphicsSystem->get(shadowPipeline)->isReady())
			return false;

		if (!shadowDescriptorSet && shadowPipeline)
		{
			auto uniforms = getShadowUniforms();
			if (uniforms.empty())
				return false;

			shadowDescriptorSet = graphicsSystem->createDescriptorSet(shadowPipeline, std::move(uniforms));
			#if GARDEN_DEBUG
			SET_RESOURCE_DEBUG_NAME(shadowDescriptorSet, "descriptorSet." + debugResourceName + ".shadow");
			#endif
		}
	}
	else
	{
		if (!basePipeline || !graphicsSystem->get(basePipeline)->isReady())
			return false;

		if (!baseDescriptorSet)
		{
			auto uniforms = getBaseUniforms();
			if (uniforms.empty())
				return false;

			baseDescriptorSet = graphicsSystem->createDescriptorSet(basePipeline, std::move(uniforms));
			#if GARDEN_DEBUG
			SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet." + debugResourceName + ".base");
			#endif
		}
	}
	return true;
}

//**********************************************************************************************************************
void InstanceRenderSystem::prepareDraw(const f32x4x4& viewProj, uint32 drawCount, bool isShadowPass)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	swapchainIndex = graphicsSystem->getSwapchainIndex();

	if (isShadowPass)
	{
		auto dataBinarySize = (shadowDrawIndex + drawCount) * getShadowInstanceDataSize();
		if (graphicsSystem->get(shadowInstanceBuffers[0][0])->getBinarySize() < dataBinarySize)
		{
			graphicsSystem->destroy(shadowInstanceBuffers);
			createInstanceBuffers(dataBinarySize, shadowInstanceBuffers, true, this);

			if (shadowDescriptorSet)
			{
				graphicsSystem->destroy(shadowDescriptorSet);
				auto uniforms = getShadowUniforms();
				shadowDescriptorSet = graphicsSystem->createDescriptorSet(shadowPipeline, std::move(uniforms));
				#if GARDEN_DEBUG
				SET_RESOURCE_DEBUG_NAME(shadowDescriptorSet, "descriptorSet." + debugResourceName + ".shadow");
				#endif
			}
		}

		auto bufferView = graphicsSystem->get(shadowInstanceBuffers[swapchainIndex][0]);
		instanceMap = bufferView->getMap();
		pipelineView = graphicsSystem->get(shadowPipeline);
		descriptorSet = shadowDescriptorSet;
	}
	else
	{
		auto dataBinarySize = drawCount * getBaseInstanceDataSize();
		if (graphicsSystem->get(baseInstanceBuffers[0][0])->getBinarySize() < dataBinarySize)
		{
			graphicsSystem->destroy(baseInstanceBuffers);
			createInstanceBuffers(dataBinarySize, baseInstanceBuffers, false, this);

			if (baseDescriptorSet)
			{
				graphicsSystem->destroy(baseDescriptorSet);
				auto uniforms = getBaseUniforms();
				baseDescriptorSet = graphicsSystem->createDescriptorSet(basePipeline, std::move(uniforms));
				#if GARDEN_DEBUG
				SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet." + debugResourceName + ".base");
				#endif
			}
		}

		auto bufferView = graphicsSystem->get(baseInstanceBuffers[swapchainIndex][0]);
		instanceMap = bufferView->getMap();
		pipelineView = graphicsSystem->get(basePipeline);
		descriptorSet = baseDescriptorSet;
		shadowDrawIndex = 0;
	}
}
void InstanceRenderSystem::beginDrawAsync(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(f32x4::zero, taskIndex);
}
void InstanceRenderSystem::finalizeDraw(const f32x4x4& viewProj, uint32 drawCount, bool isShadowPass)
{
	ID<Buffer> instanceBuffer; uint64 dataBinarySize;
	if (isShadowPass)
	{
		instanceBuffer = shadowInstanceBuffers[swapchainIndex][0];
		dataBinarySize = (shadowDrawIndex + drawCount) * getShadowInstanceDataSize();
		shadowDrawIndex += drawCount;
	}
	else
	{
		instanceBuffer = baseInstanceBuffers[swapchainIndex][0];
		dataBinarySize = drawCount * getBaseInstanceDataSize();
	}

	auto bufferView = GraphicsSystem::Instance::get()->get(instanceBuffer);
	bufferView->flush(dataBinarySize);
}

//**********************************************************************************************************************
void InstanceRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.bufferCount)
	{
		if (!baseInstanceBuffers.empty())
		{
			auto bufferSize = graphicsSystem->get(baseInstanceBuffers[0][0])->getBinarySize();
			graphicsSystem->destroy(baseInstanceBuffers);
			createInstanceBuffers(bufferSize, baseInstanceBuffers, false, this);
		}
		if (!shadowInstanceBuffers.empty())
		{
			auto bufferSize = graphicsSystem->get(shadowInstanceBuffers[0][0])->getBinarySize();
			graphicsSystem->destroy(shadowInstanceBuffers);
			createInstanceBuffers(bufferSize, shadowInstanceBuffers, false, this);
		}

		if (baseDescriptorSet)
		{
			graphicsSystem->destroy(baseDescriptorSet);
			auto uniforms = getBaseUniforms();
			baseDescriptorSet = graphicsSystem->createDescriptorSet(basePipeline, std::move(uniforms));
			#if GARDEN_DEBUG
			SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet." + debugResourceName + ".base");
			#endif
		}
		if (shadowDescriptorSet)
		{
			graphicsSystem->destroy(shadowDescriptorSet);
			auto uniforms = getShadowUniforms();
			shadowDescriptorSet = graphicsSystem->createDescriptorSet(shadowPipeline, std::move(uniforms));
			#if GARDEN_DEBUG
			SET_RESOURCE_DEBUG_NAME(shadowDescriptorSet, "descriptorSet." + debugResourceName + ".shadow");
			#endif
		}
	}
}

//**********************************************************************************************************************
map<string, DescriptorSet::Uniform> InstanceRenderSystem::getBaseUniforms()
{
	map<string, DescriptorSet::Uniform> baseUniforms =
	{ { "instance", DescriptorSet::Uniform(baseInstanceBuffers) } };
	return baseUniforms;
}
map<string, DescriptorSet::Uniform> InstanceRenderSystem::getShadowUniforms()
{
	map<string, DescriptorSet::Uniform> baseUniforms =
	{ { "instance", DescriptorSet::Uniform(shadowInstanceBuffers) } };
	return baseUniforms;
}

ID<GraphicsPipeline> InstanceRenderSystem::getBasePipeline()
{
	if (!basePipeline)
		basePipeline = createBasePipeline();
	return basePipeline;
}
ID<GraphicsPipeline> InstanceRenderSystem::getShadowPipeline()
{
	if (!shadowPipeline)
		shadowPipeline = createShadowPipeline();
	return shadowPipeline;
}