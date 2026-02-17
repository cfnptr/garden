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

#include "garden/system/render/instance.hpp"

using namespace garden;

static void createInstanceBuffers(uint64 bufferSize, DescriptorSet::Buffers& instanceBuffers, 
	bool isShadow, InstanceRenderSystem* system)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto inFlightCount = graphicsSystem->getInFlightCount();
	instanceBuffers.resize(inFlightCount);

	for (uint32 i = 0; i < inFlightCount; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Usage::Storage, Buffer::CpuAccess::SequentialWrite,
			bufferSize, Buffer::Location::Auto, Buffer::Strategy::Size);
		#if GARDEN_DEBUG || GARDEN_EDITOR
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
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", InstanceRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", InstanceRenderSystem::deinit);
}
InstanceRenderSystem::~InstanceRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", InstanceRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", InstanceRenderSystem::deinit);
	}
}

void InstanceRenderSystem::init()
{
	auto manager = Manager::Instance::get();
	if (manager->hasEvent("GBufferRecreate"))
		ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", InstanceRenderSystem::gBufferRecreate);
	else
		ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", InstanceRenderSystem::gBufferRecreate);
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

		auto manager = Manager::Instance::get();
		if (manager->hasEvent("GBufferRecreate"))
			ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", InstanceRenderSystem::gBufferRecreate);
		else
			ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", InstanceRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
bool InstanceRenderSystem::isDrawReady(int8 shadowPass)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (shadowPass < 0)
	{
		if (!basePipeline)
			basePipeline = createBasePipeline();
		if (!basePipeline)
			return false;
		if (!graphicsSystem->get(basePipeline)->isReady())
			return false;

		auto baseInstanceSize = getBaseInstanceDataSize();
		if (baseInstanceBuffers.empty() && baseInstanceSize > 0)
			createInstanceBuffers(baseInstanceSize, baseInstanceBuffers, false, this);

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
	else
	{
		if (!shadowPipeline)
			shadowPipeline = createShadowPipeline();
		if (!shadowPipeline)
			return false;
		if (!graphicsSystem->get(shadowPipeline)->isReady())
			return false;

		auto shadowInstanceSize = getShadowInstanceDataSize();
		if (shadowInstanceBuffers.empty() && shadowInstanceSize > 0)
			createInstanceBuffers(shadowInstanceSize, shadowInstanceBuffers, true, this);

		if (!shadowDescriptorSet)
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

	this->shadowPass = shadowPass;
	return true;
}

//**********************************************************************************************************************
void InstanceRenderSystem::prepareDraw(const f32x4x4& viewProj, uint32 drawCount, uint32 instanceCount, int8 shadowPass)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	inFlightIndex = graphicsSystem->getInFlightIndex();

	if (shadowPass < 0)
	{
		auto baseInstanceSize = getBaseInstanceDataSize();
		if (baseInstanceSize > 0)
		{
			auto dataBinarySize = instanceCount * baseInstanceSize;
			if (baseInstanceBuffers.empty() || graphicsSystem->get(
				baseInstanceBuffers[0][0])->getBinarySize() < dataBinarySize)
			{
				graphicsSystem->destroy(baseInstanceBuffers);
				createInstanceBuffers(dataBinarySize, baseInstanceBuffers, false, this);

				if (baseDescriptorSet)
				{
					graphicsSystem->destroy(baseDescriptorSet);

					auto uniforms = getBaseUniforms();
					if (!uniforms.empty())
					{
						baseDescriptorSet = graphicsSystem->createDescriptorSet(basePipeline, std::move(uniforms));
						#if GARDEN_DEBUG
						SET_RESOURCE_DEBUG_NAME(baseDescriptorSet, "descriptorSet." + debugResourceName + ".base");
						#endif
					}
				}
			}

			auto bufferView = graphicsSystem->get(baseInstanceBuffers[inFlightIndex][0]);
			instanceMap = bufferView->getMap();
		}
		
		descriptorSet = baseDescriptorSet;
		pipelineView = OptView<GraphicsPipeline>(graphicsSystem->get(basePipeline));
	}
	else
	{
		auto shadowInstanceSize = getBaseInstanceDataSize();
		if (shadowInstanceSize > 0)
		{
			auto dataBinarySize = (shadowInstanceIndex + instanceCount) * shadowInstanceSize;
			if (shadowInstanceBuffers.empty() || graphicsSystem->get(
				shadowInstanceBuffers[0][0])->getBinarySize() < dataBinarySize)
			{
				graphicsSystem->destroy(shadowInstanceBuffers);
				createInstanceBuffers(dataBinarySize, shadowInstanceBuffers, true, this);

				if (shadowDescriptorSet)
				{
					graphicsSystem->destroy(shadowDescriptorSet);

					auto uniforms = getShadowUniforms();
					if (!uniforms.empty())
					{
						shadowDescriptorSet = graphicsSystem->createDescriptorSet(shadowPipeline, std::move(uniforms));
						#if GARDEN_DEBUG
						SET_RESOURCE_DEBUG_NAME(shadowDescriptorSet, "descriptorSet." + debugResourceName + ".shadow");
						#endif
					}
				}
			}

			auto bufferView = graphicsSystem->get(shadowInstanceBuffers[inFlightIndex][0]);
			instanceMap = bufferView->getMap();
		}
		
		descriptorSet = shadowDescriptorSet;
		pipelineView = OptView<GraphicsPipeline>(graphicsSystem->get(shadowPipeline));
		pipelineView->updateFramebuffer(graphicsSystem->getCurrentFramebuffer());
	}
}

//**********************************************************************************************************************
void InstanceRenderSystem::beginDrawAsync(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(float4::zero, taskIndex);
}
void InstanceRenderSystem::finalizeDraw(uint32 instanceCount)
{
	if (shadowPass < 0)
	{
		if (!baseInstanceBuffers.empty())
		{
			auto instanceBuffer = baseInstanceBuffers[inFlightIndex][0];
			auto bufferView = GraphicsSystem::Instance::get()->get(instanceBuffer);
			bufferView->flush(instanceCount * getBaseInstanceDataSize());
		}
	}
	else
	{
		shadowInstanceIndex += instanceCount;
	}
}
void InstanceRenderSystem::renderCleanup()
{
	if (shadowInstanceIndex > 0)
	{
		if (!shadowInstanceBuffers.empty())
		{
			auto instanceBuffer = shadowInstanceBuffers[inFlightIndex][0];
			auto bufferView = GraphicsSystem::Instance::get()->get(instanceBuffer);
			bufferView->flush(shadowInstanceIndex * getShadowInstanceDataSize());
		}
		shadowInstanceIndex = 0;
	}
}

void InstanceRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(baseDescriptorSet);
	graphicsSystem->destroy(shadowDescriptorSet);
}

//**********************************************************************************************************************
DescriptorSet::Uniforms InstanceRenderSystem::getBaseUniforms()
{
	if (baseInstanceBuffers.empty())
		return {};
	DescriptorSet::Uniforms baseUniforms = { { "instance", DescriptorSet::Uniform(baseInstanceBuffers) } };
	return baseUniforms;
}
DescriptorSet::Uniforms InstanceRenderSystem::getShadowUniforms()
{
	if (shadowInstanceBuffers.empty())
		return {};
	DescriptorSet::Uniforms baseUniforms = { { "instance", DescriptorSet::Uniform(shadowInstanceBuffers) } };
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