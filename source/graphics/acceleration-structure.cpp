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
			if (pipelineView->isBindless())
				continue;

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
						GARDEN_ASSERT_MSG(storageBuffer != ID<Buffer>(resource), 
							"Descriptor set [" + descriptorSet.getDebugName() + "] is "
							"still using destroyed AS storage [" + debugName + "]");
					}
				}
			}
		}
		if (type == AccelerationStructure::Type::Blas)
		{
			for (auto& tlas : graphicsAPI->tlasPool)
			{
				if (!tlas.instance)
					continue;

				for (const auto& instance : TlasExt::getInstances(tlas))
				{
					auto blasView = graphicsAPI->blasPool.get(instance.blas);
					GARDEN_ASSERT_MSG(this->instance != blasView->instance, "TLAS [" + tlas.debugName + 
						"] is still using destroyed BLAS [" + debugName + "]");
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

		if (buildData)
		{
			auto buildDataHeader = (BuildDataHeader*)buildData;
			auto data = (CompactData*)buildDataHeader->compactData;
			if (data)
			{
				if (data->queryPoolRef == 0)
				{
					vulkanAPI->device.destroyQueryPool((VkQueryPool)data->queryPool);
					delete data;
				}
				else data->queryPoolRef--;
			}
		}
		free(buildData);
	}
	else abort();

	return true;
}

bool AccelerationStructure::isStorageReady() const noexcept
{
	auto storageView = GraphicsAPI::get()->bufferPool.get(storageBuffer);
	return storageView->isReady();
}

//**********************************************************************************************************************
void AccelerationStructure::build(ID<Buffer> scratchBuffer)
{
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer != 
		GraphicsAPI::get()->frameCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(buildData, "Acceleration structure [" + debugName + "] is already build");

	auto graphicsAPI = GraphicsAPI::get();
	auto buildDataHeader = (const BuildDataHeader*)buildData;

	#if GARDEN_DEBUG
	if (graphicsAPI->currentCommandBuffer == GraphicsAPI::get()->computeCommandBuffer)
	{
		GARDEN_ASSERT_MSG(hasAnyFlag(flags, BuildFlagsAS::ComputeQ), 
			"Acceleration structure [" + debugName + "] does not have compute queue flag");

		if (scratchBuffer)
		{
			auto bufferView = graphicsAPI->bufferPool.get(scratchBuffer);
			GARDEN_ASSERT_MSG(hasAnyFlag(bufferView->getUsage(), Buffer::Usage::ComputeQ), 
				"Scratch buffer [" + bufferView->getDebugName() + "] does not have compute queue flag");
		}
	}
	#endif

	auto destroyScratch = false;
	if (!scratchBuffer)
	{
		scratchBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::Storage | 
			Buffer::Usage::DeviceAddress, Buffer::CpuAccess::None, Buffer::Location::PreferGPU, 
			Buffer::Strategy::Speed, buildDataHeader->scratchSize , 0);
		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto bufferView = graphicsAPI->bufferPool.get(scratchBuffer);
		bufferView->setDebugName(debugName + ".scratchBuffer");
		#endif
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

	auto bufferView = graphicsAPI->bufferPool.get(storageBuffer);
	ResourceExt::getBusyLock(**bufferView)++;
	bufferView = graphicsAPI->bufferPool.get(scratchBuffer);
	ResourceExt::getBusyLock(**bufferView)++;
	busyLock++;

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	currentCommandBuffer->addLockedResource(storageBuffer);
	currentCommandBuffer->addLockedResource(scratchBuffer);

	if (type == AccelerationStructure::Type::Blas)
		currentCommandBuffer->addLockedResource(ID<Blas>(command.dstAS));
	else
		currentCommandBuffer->addLockedResource(ID<Tlas>(command.dstAS));

	auto syncBuffers = (ID<Buffer>*)((uint8*)buildData + sizeof(AccelerationStructure::BuildDataHeader));
	for (uint32 i = 0; i < buildDataHeader->bufferCount; i++)
	{
		auto buffer = syncBuffers[i];
		bufferView = graphicsAPI->bufferPool.get(buffer);
		ResourceExt::getBusyLock(**bufferView)++;
		currentCommandBuffer->addLockedResource(buffer);
	}

	if (destroyScratch)
		graphicsAPI->bufferPool.destroy(scratchBuffer);
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void AccelerationStructure::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	auto storageView = GraphicsAPI::get()->bufferPool.get(storageBuffer);
	storageView->setDebugName("asBuffer.storage." + name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils || !instance)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eAccelerationStructureKHR, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		#endif
	}
	else abort();
}
#endif

//**********************************************************************************************************************
void AccelerationStructure::_createVkInstance(uint64 size, uint8 type, 
	BuildFlagsAS flags, ID<Buffer>& storageBuffer, void*& instance, uint64& deviceAddress)
{
	auto vulkanAPI = VulkanAPI::get();
	auto storageUsage = Buffer::Usage::StorageAS | Buffer::Usage::DeviceAddress;
	if (hasAnyFlag(flags, BuildFlagsAS::ComputeQ))
		storageUsage |= Buffer::Usage::ComputeQ;
	auto storageStrategy = hasAnyFlag(flags, BuildFlagsAS::PreferFastBuild) ? 
		Buffer::Strategy::Speed : Buffer::Strategy::Size;
	storageBuffer = vulkanAPI->bufferPool.create(storageUsage, Buffer::CpuAccess::None, 
		Buffer::Location::PreferGPU, storageStrategy, size, 0);
	auto storageView = vulkanAPI->bufferPool.get(storageBuffer);

	vk::AccelerationStructureCreateInfoKHR createInfo;
	createInfo.buffer = (VkBuffer)ResourceExt::getInstance(**storageView);
	createInfo.size = size; createInfo.type = (vk::AccelerationStructureTypeKHR)type;
	auto accelerationStructure = vulkanAPI->device.createAccelerationStructureKHR(createInfo);
	instance = accelerationStructure;

	vk::AccelerationStructureDeviceAddressInfoKHR addressInfo;
	addressInfo.accelerationStructure = accelerationStructure;
	deviceAddress = vulkanAPI->device.getAccelerationStructureAddressKHR(addressInfo);
}