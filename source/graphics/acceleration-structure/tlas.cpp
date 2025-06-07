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

#include "garden/graphics/acceleration-structure/tlas.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace std;
using namespace math;
using namespace garden;
using namespace garden::graphics;

static constexpr VkGeometryInstanceFlagsKHR toVkInstanceFlagsAS(Tlas::InstanceFlags tlasInstanceFlags) noexcept
{
	VkGeometryInstanceFlagsKHR flags = 0;
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::DisableCulling))
		flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::FlipFacing))
		flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::ForceOpaque))
		flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::ForceNoOpaque))
		flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
	return flags;
}

//**********************************************************************************************************************
static void createVkTlas(ID<Buffer> instanceBuffer, BuildFlagsAS flags, 
	ID<Buffer>& storage, void*& instance, uint64& deviceAddress, void*& _buildData)
{
	auto vulkanAPI = VulkanAPI::get();
	auto buildData = malloc<uint8>(sizeof(AccelerationStructure::BuildDataHeader) + sizeof(ID<Buffer>) +
		sizeof(vk::AccelerationStructureGeometryKHR) + sizeof(vk::AccelerationStructureBuildRangeInfoKHR));
	_buildData = buildData;

	auto buildDataHeader = (AccelerationStructure::BuildDataHeader*)buildData;
	auto buildDataOffset = sizeof(AccelerationStructure::BuildDataHeader);
	auto syncBuffer = (ID<Buffer>*)(buildData + buildDataOffset);
	buildDataOffset += sizeof(ID<Buffer>);
	auto geometryAS = (vk::AccelerationStructureGeometryKHR*)(buildData + buildDataOffset);
	buildDataOffset += sizeof(vk::AccelerationStructureGeometryKHR);
	auto rangeInfo = (vk::AccelerationStructureBuildRangeInfoKHR*)(buildData + buildDataOffset);

	auto instanceBufferView = vulkanAPI->bufferPool.get(instanceBuffer);
	GARDEN_ASSERT(hasAnyFlag(instanceBufferView->getUsage(), Buffer::Usage::DeviceAddress));
	GARDEN_ASSERT_MSG(instanceBufferView->getDeviceAddress(), "Instance buffer [" + 
		instanceBufferView->getDebugName() + "] is not ready");
	auto instanceCount = instanceBufferView->getBinarySize() / sizeof(vk::AccelerationStructureInstanceKHR);

	vk::AccelerationStructureGeometryKHR geometry;
	geometry.geometryType = vk::GeometryTypeKHR::eInstances;
	geometry.geometry.instances = vk::AccelerationStructureGeometryInstancesDataKHR();
	geometry.geometry.instances.data.deviceAddress = instanceBufferView->getDeviceAddress();
	*geometryAS = geometry;
	*rangeInfo = vk::AccelerationStructureBuildRangeInfoKHR(instanceCount, 0, 0, 0);
	*syncBuffer = instanceBuffer;

	vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo;
	geometryInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	geometryInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	geometryInfo.flags = toVkBuildFlagsAS(flags);
	geometryInfo.geometryCount = 1;
	geometryInfo.pGeometries = geometryAS;

	auto sizesInfo = vulkanAPI->device.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, geometryInfo, instanceCount);

	auto storageUsage = Buffer::Usage::StorageAS | Buffer::Usage::DeviceAddress;
	if (hasAnyFlag(flags, BuildFlagsAS::ComputeQ))
		storageUsage |= Buffer::Usage::ComputeQ;
	auto storageStrategy = hasAnyFlag(flags, BuildFlagsAS::PreferFastBuild) ? 
		Buffer::Strategy::Speed : Buffer::Strategy::Size;
	storage = vulkanAPI->bufferPool.create(storageUsage, Buffer::CpuAccess::None, 
		Buffer::Location::PreferGPU, storageStrategy, sizesInfo.accelerationStructureSize, 0);
	auto storageView = vulkanAPI->bufferPool.get(storage);

	vk::AccelerationStructureCreateInfoKHR createInfo;
	createInfo.buffer = (VkBuffer)ResourceExt::getInstance(**storageView);
	createInfo.size = sizesInfo.accelerationStructureSize;
	createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	auto accelerationStructure = vulkanAPI->device.createAccelerationStructureKHR(createInfo);
	instance = accelerationStructure;

	vk::AccelerationStructureDeviceAddressInfoKHR addressInfo;
	addressInfo.accelerationStructure = accelerationStructure;
	deviceAddress = vulkanAPI->device.getAccelerationStructureAddressKHR(addressInfo);

	buildDataHeader->scratchSize = sizesInfo.buildScratchSize +
		vulkanAPI->asProperties.minAccelerationStructureScratchOffsetAlignment;
	buildDataHeader->geometryCount = buildDataHeader->bufferCount = 1;
}

Tlas::InstanceData::InstanceData(const f32x4x4& model, ID<Blas> blas, 
	uint32 customIndex, uint32 sbtRecordOffset, uint8 mask, InstanceFlags flags) noexcept : 
	blas(blas), customIndex(customIndex), sbtRecordOffset(sbtRecordOffset), mask(mask), flags(flags)
{
	GARDEN_ASSERT(blas);
	auto transModel = transpose4x4(model);
	memcpy(transform, &transModel, sizeof(float) * 3 * 4);
}

//**********************************************************************************************************************
Tlas::Tlas(vector<InstanceData>&& instances, ID<Buffer> instanceBuffer, BuildFlagsAS flags) : 
	AccelerationStructure(1, flags, Type::Tlas), instances(std::move(instances))
{
	GARDEN_ASSERT(!this->instances.empty());
	GARDEN_ASSERT(instanceBuffer);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkTlas(instanceBuffer, flags, storageBuffer, instance, deviceAddress, buildData);
	else abort();
}

uint32 Tlas::getInstanceSize() noexcept
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		return sizeof(vk::AccelerationStructureInstanceKHR);
	else abort();
}
void Tlas::getInstanceData(const InstanceData* instanceArray, uint32 instanceCount, uint8* data) noexcept
{
	GARDEN_ASSERT(instanceArray);
	GARDEN_ASSERT(instanceCount > 0);
	GARDEN_ASSERT(data);

	auto instances = (vk::AccelerationStructureInstanceKHR*)data;
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		for (uint32 i = 0; i < instanceCount; i++)
		{
			auto instance = instanceArray[i];
			auto asView = vulkanAPI->blasPool.get(instance.blas);

			VkAccelerationStructureInstanceKHR vkInstance;
			memcpy(vkInstance.transform.matrix, instance.transform, sizeof(float) * 3 * 4);
			vkInstance.instanceCustomIndex = instance.customIndex;
			vkInstance.mask = instance.mask;
			vkInstance.instanceShaderBindingTableRecordOffset = instance.sbtRecordOffset;
			vkInstance.flags = toVkInstanceFlagsAS(instance.flags);
			vkInstance.accelerationStructureReference = AccelerationStructureExt::getDeviceAddress(**asView); 
			instances[i] = vkInstance;
		}
	}
	else abort();
}

void Tlas::build(ID<Buffer> scratchBuffer)
{
	AccelerationStructure::build(scratchBuffer);
	auto graphicsAPI = GraphicsAPI::get();
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;

	for (const auto& instance : instances)
	{
		auto blasView = graphicsAPI->blasPool.get(instance.blas);
		ResourceExt::getBusyLock(**blasView)++;
		currentCommandBuffer->addLockedResource(instance.blas);
	}
}