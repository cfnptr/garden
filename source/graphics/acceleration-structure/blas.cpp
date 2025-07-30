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

#include "garden/graphics/acceleration-structure/blas.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace std;
using namespace math;
using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static void prepareVkBlas(VulkanAPI* vulkanAPI, 
	const Blas::TrianglesBuffer* geometryArray, uint32 geometryCount, vk::AccelerationStructureGeometryKHR* asArray, 
	vk::AccelerationStructureBuildRangeInfoKHR* rangeInfos, ID<Buffer>* syncBuffers, BuildFlagsAS flags)
{
	for (uint32 i = 0; i < geometryCount; i++)
	{
		auto geometry = geometryArray[i];
		GARDEN_ASSERT(geometry.vertexBuffer);
		GARDEN_ASSERT(geometry.indexBuffer);
		GARDEN_ASSERT(geometry.vertexSize > 0);
		GARDEN_ASSERT(geometry.vertexSize % 4 == 0);

		auto vertexBufferView = vulkanAPI->bufferPool.get(geometry.vertexBuffer);
		GARDEN_ASSERT(hasAnyFlag(vertexBufferView->getUsage(), Buffer::Usage::DeviceAddress));
		GARDEN_ASSERT_MSG(vertexBufferView->getDeviceAddress(), "Vertex buffer [" + 
			vertexBufferView->getDebugName() + "] is not ready");
		GARDEN_ASSERT(geometry.vertexCount + geometry.vertexOffset <= 
			vertexBufferView->getBinarySize() / geometry.vertexSize);

		auto indexBufferView = vulkanAPI->bufferPool.get(geometry.vertexBuffer);
		GARDEN_ASSERT(hasAnyFlag(indexBufferView->getUsage(), Buffer::Usage::DeviceAddress));
		GARDEN_ASSERT_MSG(indexBufferView->getDeviceAddress(), "Index buffer [" + 
			indexBufferView->getDebugName() + "] is not ready");
		GARDEN_ASSERT(geometry.primitiveCount + geometry.primitiveOffset <= 
			indexBufferView->getBinarySize() / (toBinarySize(geometry.indexType) * 3));

		vk::AccelerationStructureGeometryKHR as;
		as.geometryType = vk::GeometryTypeKHR::eTriangles;
		as.geometry.triangles = vk::AccelerationStructureGeometryTrianglesDataKHR();
		as.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
		as.geometry.triangles.vertexData.deviceAddress = vertexBufferView->getDeviceAddress(); // TODO: support case when pos is not first component.
		as.geometry.triangles.vertexStride = geometry.vertexSize;
		as.geometry.triangles.maxVertex = (geometry.vertexCount > 0 ?  geometry.vertexCount : 
			vertexBufferView->getBinarySize() / geometry.vertexSize - geometry.vertexOffset) - 1; // Note: -1 required!
		as.geometry.triangles.indexType = toVkIndexType(geometry.indexType);
		as.geometry.triangles.indexData.deviceAddress = indexBufferView->getDeviceAddress();
		as.geometry.triangles.transformData = nullptr; // Identity transform TODO:
		
		if (geometry.isOpaqueOnly)
			as.flags = vk::GeometryFlagBitsKHR::eOpaque;
		if (geometry.noDuplicateAnyHit)
			as.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
		asArray[i] = as;

		vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
		rangeInfo.primitiveCount = geometry.primitiveCount > 0 ? geometry.primitiveCount :
			indexBufferView->getBinarySize() / (toBinarySize(geometry.indexType) * 3) - geometry.primitiveOffset;
		rangeInfo.primitiveOffset = geometry.primitiveOffset;
		rangeInfo.firstVertex = geometry.vertexOffset;
		rangeInfo.transformOffset = 0; // TODO:
		rangeInfos[i] = rangeInfo;

		syncBuffers[i * 2] = geometry.vertexBuffer;
		syncBuffers[i * 2 + 1] = geometry.indexBuffer;
	}
}

//**********************************************************************************************************************
static void prepareVkBlas(VulkanAPI* vulkanAPI, 
	const Blas::AabbsBuffer* geometryArray, uint32 geometryCount, vk::AccelerationStructureGeometryKHR* asArray, 
	vk::AccelerationStructureBuildRangeInfoKHR* rangeInfos, ID<Buffer>* syncBuffers, BuildFlagsAS flags)
{
	for (uint32 i = 0; i < geometryCount; i++)
	{
		auto geometry = geometryArray[i];
		GARDEN_ASSERT(geometry.aabbBuffer);
		GARDEN_ASSERT(geometry.aabbStride > 0);
		GARDEN_ASSERT(geometry.aabbStride % 8 == 0);

		auto aabbBufferView = vulkanAPI->bufferPool.get(geometry.aabbBuffer);
		GARDEN_ASSERT(hasAnyFlag(aabbBufferView->getUsage(), Buffer::Usage::DeviceAddress));
		GARDEN_ASSERT_MSG(aabbBufferView->getDeviceAddress(), "AABB buffer [" + 
			aabbBufferView->getDebugName() + "] is not ready");
		GARDEN_ASSERT(geometry.aabbCount + geometry.aabbOffset <= 
			aabbBufferView->getBinarySize() / geometry.aabbStride);

		vk::AccelerationStructureGeometryKHR as;
		as.geometryType = vk::GeometryTypeKHR::eAabbs;
		as.geometry.aabbs = vk::AccelerationStructureGeometryAabbsDataKHR();
		as.geometry.aabbs.data.deviceAddress = aabbBufferView->getDeviceAddress();
		as.geometry.aabbs.stride = geometry.aabbStride;
		
		if (geometry.isOpaqueOnly)
			as.flags = vk::GeometryFlagBitsKHR::eOpaque;
		if (geometry.noDuplicateAnyHit)
			as.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
		asArray[i] = as;

		vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
		rangeInfo.primitiveCount = geometry.aabbCount > 0 ? geometry.aabbCount :
			aabbBufferView->getBinarySize() / geometry.aabbStride - geometry.aabbOffset;
		rangeInfo.primitiveOffset = geometry.aabbOffset;
		rangeInfo.firstVertex = 0;
		rangeInfo.transformOffset = 0;
		rangeInfos[i] = rangeInfo;

		syncBuffers[i] = geometry.aabbBuffer;
	}
}

//**********************************************************************************************************************
static void createVkBlas(const void* geometryArray, uint32 geometryCount, uint8 geometryType,
	BuildFlagsAS flags, ID<Buffer>& storageBuffer, void*& instance, uint64& deviceAddress, void*& _buildData)
{
	auto vulkanAPI = VulkanAPI::get();
	auto buildData = malloc<uint8>(
		sizeof(AccelerationStructure::BuildDataHeader) + geometryCount * (geometryType * sizeof(ID<Buffer>) +
		sizeof(vk::AccelerationStructureGeometryKHR) + sizeof(vk::AccelerationStructureBuildRangeInfoKHR)));
	_buildData = buildData;

	auto buildDataHeader = (AccelerationStructure::BuildDataHeader*)buildData;
	auto buildDataOffset = sizeof(AccelerationStructure::BuildDataHeader);
	auto syncBuffers = (ID<Buffer>*)(buildData + buildDataOffset);
	buildDataOffset += geometryCount * geometryType * sizeof(ID<Buffer>);
	auto asArray = (vk::AccelerationStructureGeometryKHR*)(buildData + buildDataOffset);
	buildDataOffset += geometryCount * sizeof(vk::AccelerationStructureGeometryKHR);
	auto rangeInfos = (vk::AccelerationStructureBuildRangeInfoKHR*)(buildData + buildDataOffset);

	if (geometryType == 2)
	{
		prepareVkBlas(vulkanAPI, (const Blas::TrianglesBuffer*)geometryArray, 
			geometryCount, asArray, rangeInfos, syncBuffers, flags);
	}
	else
	{
		prepareVkBlas(vulkanAPI, (const Blas::AabbsBuffer*)geometryArray, 
			geometryCount, asArray, rangeInfos, syncBuffers, flags);
	}

	vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo;
	geometryInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	geometryInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	geometryInfo.flags = toVkBuildFlagsAS(flags);
	geometryInfo.geometryCount = geometryCount;
	geometryInfo.pGeometries = asArray;

	vector<uint32_t> maxPrimitiveCounts(geometryCount);
	for (uint32 i = 0; i < geometryCount; i++)
		maxPrimitiveCounts[i] = rangeInfos[i].primitiveCount;

	// TODO: also support building on the host (CPU). Currently only relevant for AMD and mobile GPUs.
	auto sizesInfo = vulkanAPI->device.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, geometryInfo, maxPrimitiveCounts);
	AccelerationStructure::_createVkInstance(sizesInfo.accelerationStructureSize, 
		(uint8)vk::AccelerationStructureTypeKHR::eBottomLevel, flags, storageBuffer, instance, deviceAddress);

	buildDataHeader->scratchSize = sizesInfo.buildScratchSize +
		vulkanAPI->asProperties.minAccelerationStructureScratchOffsetAlignment;
	buildDataHeader->geometryCount = geometryCount;
	buildDataHeader->bufferCount = geometryCount * geometryType;
	buildDataHeader->queryPoolIndex = 0;
}

//**********************************************************************************************************************
Blas::Blas(const Blas::TrianglesBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags) : 
	AccelerationStructure(geometryCount, flags, Type::Blas)
{
	GARDEN_ASSERT(geometryArray);
	GARDEN_ASSERT(geometryCount > 0);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkBlas(geometryArray, geometryCount, 2, flags, storageBuffer, instance, deviceAddress, buildData);
	else abort();
}
Blas::Blas(const Blas::AabbsBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags) :
	AccelerationStructure(geometryCount, flags, Type::Blas)
{
	GARDEN_ASSERT(geometryArray);
	GARDEN_ASSERT(geometryCount > 0);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkBlas(geometryArray, geometryCount, 1, flags, storageBuffer, instance, deviceAddress, buildData);
	else abort();
}
Blas::Blas(uint64 size, BuildFlagsAS flags)
{
	GARDEN_ASSERT(size > 0);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		AccelerationStructure::_createVkInstance(size, (uint8)vk::AccelerationStructureTypeKHR::eBottomLevel, 
			flags, storageBuffer, instance, deviceAddress);
	}
	else abort();
}

//**********************************************************************************************************************
ID<Blas> Blas::compact()
{
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer != 
		GraphicsAPI::get()->frameCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(hasAnyFlag(flags, BuildFlagsAS::AllowCompaction), 
		"BLAS [" + debugName + "] compaction is not allowed");
	GARDEN_ASSERT_MSG(buildData, "BLAS [" + debugName + "] is already compacted");
	GARDEN_ASSERT_MSG(isStorageReady(), "BLAS [" + debugName + "] storage is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto buildDataHeader = (const BuildDataHeader*)buildData;
	auto data = (CompactData*)buildDataHeader->compactData;

	uint64 compactSize;
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!data->queryResults[0])
		{
			auto vkResult = vulkanAPI->device.getQueryPoolResults((VkQueryPool)data->queryPool, 
				0, data->queryResults.size(), sizeof(uint64), data->queryResults.data(), 
				sizeof(uint64), vk::QueryResultFlagBits::eWait);
			if (vkResult != vk::Result::eSuccess)
				throw GardenError("Failed to get compacted BLAS sizes.");
		}
		compactSize = (uint64)data->queryResults[buildDataHeader->queryPoolIndex];

		if (data->queryPoolRef == 0)
		{
			vulkanAPI->device.destroyQueryPool((VkQueryPool)data->queryPool);
			delete data;
		}
		else data->queryPoolRef--;
	}
	else abort();

	free(buildData);
	buildData = nullptr;

	auto thisBlas = graphicsAPI->blasPool.getID((const Blas*)this); // Note: Do not move!
	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto thisDebugName = debugName;
	#endif

	auto compactBlas = graphicsAPI->blasPool.create(compactSize, flags);
	auto compactBlasView = graphicsAPI->blasPool.get(compactBlas);
	#if GARDEN_DEBUG || GARDEN_EDITOR
	compactBlasView->setDebugName(thisDebugName);
	#endif
	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;

	CopyAccelerationStructureCommand command;
	command.isCompact = true;
	command.typeAS = Type::Blas;
	command.srcAS = ID<AccelerationStructure>(thisBlas);
	command.dstAS = ID<AccelerationStructure>(compactBlas);
	currentCommandBuffer->addCommand(command);

	// Note: blasPool.create() call invalidates this instance.
	auto thisBlasView = graphicsAPI->blasPool.get(thisBlas);
	ResourceExt::getBusyLock(**thisBlasView)++;
	ResourceExt::getBusyLock(**compactBlasView)++;
	auto storageView = graphicsAPI->bufferPool.get(thisBlasView->getStorageBuffer());
	ResourceExt::getBusyLock(**storageView)++;
	storageView = graphicsAPI->bufferPool.get(compactBlasView->getStorageBuffer());
	ResourceExt::getBusyLock(**storageView)++;

	currentCommandBuffer->addLockedResource(thisBlas);
	currentCommandBuffer->addLockedResource(compactBlas);
	currentCommandBuffer->addLockedResource(thisBlasView->getStorageBuffer());
	currentCommandBuffer->addLockedResource(compactBlasView->getStorageBuffer());
	return compactBlas;
}