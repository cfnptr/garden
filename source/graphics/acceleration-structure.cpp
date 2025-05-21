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

#include "garden/graphics/acceleration-structure.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace std;
using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
bool AccelerationStructure::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	auto graphicsAPI = GraphicsAPI::get();

	#if GARDEN_DEBUG
	if (!graphicsAPI->forceResourceDestroy)
	{
		for (auto& descriptorSet : graphicsAPI->descriptorSetPool)
		{
			if (!ResourceExt::getInstance(descriptorSet))
				continue;

			const auto& descriptorUniforms = descriptorSet.getUniforms();
			auto pipelineView = graphicsAPI->getPipelineView(
				descriptorSet.getPipelineType(), descriptorSet.getPipeline());
			const auto& uniforms = pipelineView->getUniforms();

			for (const auto& pair : descriptorUniforms)
			{
				const auto uniform = uniforms.find(pair.first);
				if (uniform == uniforms.end() || !isBufferType(uniform->second.type) ||
					uniform->second.descriptorSetIndex != descriptorSet.getIndex())
				{
					continue;
				}

				const auto& resourceSets = pair.second.resourceSets;
				for (const auto& resourceArray : resourceSets)
				{
					for (auto resource : resourceArray)
					{
						if (ID<Buffer>(resource) != storageBuffer)
							continue;
						throw GardenError("Descriptor set is still using destroyed AS storage. (storage: " +
							debugName + ", descriptorSet: " + descriptorSet.getDebugName() + ")");
					}
				}
			}
		}
	}
	#endif

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (vulkanAPI->forceResourceDestroy)
		{
			vulkanAPI->device.destroyAccelerationStructureKHR((VkAccelerationStructureKHR)instance);
		}
		else
		{
			auto resourceType = type == Type::Blas ? 
				GraphicsAPI::DestroyResourceType::Blas : GraphicsAPI::DestroyResourceType::Tlas;
			vulkanAPI->destroyResource(resourceType, instance);
		}
		vulkanAPI->bufferPool.destroy(storageBuffer);
	}
	else abort();

	return true;
}

bool AccelerationStructure::isStorageReady() const noexcept
{
	if (!isBuilt())
		return false;
	auto storageView = GraphicsAPI::get()->bufferPool.get(storageBuffer);
	return storageView->isReady();
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void AccelerationStructure::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	auto storageView = GraphicsAPI::get()->bufferPool.get(storageBuffer);
	storageView->setDebugName("buffer." + name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eAccelerationStructureKHR, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
	}
	else abort();
}
#endif

//**********************************************************************************************************************
void AccelerationStructure::build(ID<Buffer> scratchBuffer)
{
	GARDEN_ASSERT(!isBuilt());
	GARDEN_ASSERT(!GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer != GraphicsAPI::get()->frameCommandBuffer);
	auto graphicsAPI = GraphicsAPI::get();

	auto destroyScratch = false;
	if (!scratchBuffer)
	{
		auto data = (const BuildDataHeader*)buildData;
		scratchBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::DeviceAddress | Buffer::Usage::Storage, 
			Buffer::CpuAccess::None, Buffer::Location::PreferGPU, Buffer::Strategy::Speed, data->scratchSize , 0);
		destroyScratch = true;
	}

	BuildAccelerationStructureCommand command;
	command.isUpdate = false;
	command.typeAS = type;
	command.srcAS = {};
	if (type == AccelerationStructure::Type::Blas)
		command.dstAS = ID<AccelerationStructure>(graphicsAPI->blasPool.getID((const Blas*)this));
	else
		command.dstAS = ID<AccelerationStructure>(graphicsAPI->tlasPool.getID((const Tlas*)this));
	command.scratchBuffer = scratchBuffer;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	// Note: assuming that acceleration structure will be built on a separate compute queue.
	auto bufferView = graphicsAPI->bufferPool.get(storageBuffer);
	ResourceExt::getBusyLock(**bufferView)++;
	bufferView = graphicsAPI->bufferPool.get(scratchBuffer);
	ResourceExt::getBusyLock(**bufferView)++;
	busyLock++;

	graphicsAPI->currentCommandBuffer->addLockedResource(storageBuffer);
	graphicsAPI->currentCommandBuffer->addLockedResource(scratchBuffer);

	if (type == AccelerationStructure::Type::Blas)
		graphicsAPI->currentCommandBuffer->addLockedResource(ID<Blas>(command.dstAS));
	else
		graphicsAPI->currentCommandBuffer->addLockedResource(ID<Tlas>(command.dstAS));

	if (destroyScratch)
		graphicsAPI->bufferPool.destroy(scratchBuffer);
}

/* TODO: add compact acceleration structure operation with ability to pass custom query pool. Add query pool abstraction.

	vk::QueryPool queryPool;
	if (hasAnyFlag(flags, BuildFlagsAS::AllowCompaction))
	{
		vk::QueryPoolCreateInfo createInfo;
		createInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
		createInfo.queryCount = 1; 
		queryPool = vulkanAPI->device.createQueryPool(createInfo);
		vulkanAPI->device.resetQueryPool(queryPool, 0, 1);
	}

*/