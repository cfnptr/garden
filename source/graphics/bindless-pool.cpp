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

#include "garden/graphics/bindless-pool.hpp"
#include "garden/graphics/api.hpp"

using namespace garden;
using namespace garden::graphics;

BindlessPool::BindlessPool(ID<Pipeline> pipeline, PipelineType pipelineType, 
	DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers, uint8 index)
{
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(!uniforms.empty());
	uniformData.reserve(uniforms.size());

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	auto maxBindlessCount = pipelineView->getMaxBindlessCount();
	GARDEN_ASSERT(maxBindlessCount != UINT32_MAX);

	for (auto i = uniforms.begin(); i != uniforms.end(); i++)
	{
		GARDEN_ASSERT(i->second.resourceSets.empty());
		auto& resourceSets = i.value().resourceSets;
		resourceSets.resize(1);
		resourceSets[0].resize(maxBindlessCount);

		UniformData allocData;
		uniformData.emplace(i->first, std::move(allocData));
	}

	descriptorSet = graphicsAPI->descriptorSetPool.create(pipeline, 
		pipelineType, std::move(uniforms), std::move(samplers), index);
}
BindlessPool::~BindlessPool()
{
	GraphicsAPI::get()->descriptorSetPool.destroy(descriptorSet);
}

//**********************************************************************************************************************
uint32 BindlessPool::allocate(string_view name, ID<Resource> resource, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(resource);
	GARDEN_ASSERT(descriptorSet);

	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");
	auto& allocData = uniformData.at(name);

	uint32 allocation = UINT32_MAX;
	if (!allocData.freeAllocs.empty())
	{
		auto freeAllocs = allocData.freeAllocs.data();
		for (int64 i = (int64)allocData.freeAllocs.size() - 1; i >= 0; i--)
		{
			const auto& freeAlloc = freeAllocs[i];
			if (frameIndex < freeAlloc.second)
				continue;
			allocation = freeAlloc.first;
			allocData.freeAllocs.erase(allocData.freeAllocs.begin() + i);
			break;
		}
	}
	if (allocation == UINT32_MAX)
	{
		GARDEN_ASSERT(allocData.occupancy < uniform.value().resourceSets[0].size());
		allocation = allocData.occupancy++;
	}

	auto& resourceSet = uniform.value().resourceSets[0];
	resourceSet[allocation] = resource;
	allocData.updateBeginIndex = std::min(allocData.updateBeginIndex, allocation);
	allocData.updateEndIndex = std::max(allocData.updateEndIndex, allocation + 1);
	return allocation;
}
void BindlessPool::free(string_view name, uint32 allocation, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(descriptorSet);

	if (allocation == UINT32_MAX)
		return;

	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& allocData = uniformData.at(name);
	GARDEN_ASSERT(allocation < allocData.occupancy);

	#if GARDEN_DEBUG
	for (auto freeAlloc : allocData.freeAllocs)
		GARDEN_ASSERT(allocation != freeAlloc.first); // Already destroyed.
	#endif

	allocData.freeAllocs.emplace_back(allocation, frameIndex + frameLag + 1);
}

//**********************************************************************************************************************
void BindlessPool::update()
{
	GARDEN_ASSERT(descriptorSet);
	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);

	for (auto i = uniformData.begin(); i != uniformData.end(); i++)
	{
		auto& allocData = i.value();
		if (allocData.updateBeginIndex >= allocData.updateEndIndex)
			continue;

		auto updateCount = allocData.updateEndIndex - allocData.updateBeginIndex;
		descriptorSetView->writeResources(i->first, updateCount, allocData.updateBeginIndex);

		allocData.updateBeginIndex = UINT32_MAX;
		allocData.updateEndIndex = 0;
	}
}
void BindlessPool::destroy()
{
	GraphicsAPI::get()->descriptorSetPool.destroy(descriptorSet);
	
	descriptorSet = {};
	uniformData.clear();
}