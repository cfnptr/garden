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

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineView = graphicsAPI->getPipelineView(pipelineType, pipeline);
	auto maxBindlessCount = pipelineView->getMaxBindlessCount();
	GARDEN_ASSERT_MSG(!uniforms.empty(), "Assert " + pipelineView->getDebugName());
	GARDEN_ASSERT_MSG(maxBindlessCount != UINT32_MAX, "Assert " + pipelineView->getDebugName());

	uniformData.reserve(uniforms.size());
	for (auto i = uniforms.begin(); i != uniforms.end(); i++)
	{
		GARDEN_ASSERT_MSG(i->second.resourceSets.empty(), 
			"No resource set for uniform [" + i->first + "]");
		auto& resourceSets = i.value().resourceSets;
		resourceSets.resize(1);
		resourceSets[0].resize(maxBindlessCount);

		UniformData allocData;
		uniformData.emplace(i->first, std::move(allocData));
	}

	descriptorSet = graphicsAPI->descriptorSetPool.create(pipeline, 
		pipelineType, std::move(uniforms), std::move(samplers), index);

	auto test = graphicsAPI->descriptorSetPool.get(descriptorSet);
	test->getIndex();
}
void BindlessPool::destroy()
{
	GraphicsAPI::get()->descriptorSetPool.destroy(descriptorSet);
	
	descriptorSet = {};
	uniformData.clear();
}

//**********************************************************************************************************************
uint32 BindlessPool::allocate(string_view name, ID<Resource> resource, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(resource, "Assert " + string(name));
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
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
		GARDEN_ASSERT_MSG(allocData.occupancy < uniform.value().resourceSets[0].size(),
			"Out of maximum bindless descriptor set count");
		allocation = allocData.occupancy++;
	}

	auto& resourceSet = uniform.value().resourceSets[0];
	resourceSet[allocation] = resource;
	return allocation;
}
void BindlessPool::update(string_view name, uint32 allocation, ID<Resource> resource, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(resource, "Assert " + string(name));
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& resourceSet = uniform.value().resourceSets[0];
	GARDEN_ASSERT(allocation < resourceSet.size());
	resourceSet[allocation] = resource;
}
void BindlessPool::free(string_view name, uint32 allocation, uint64 frameIndex)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));

	if (allocation == UINT32_MAX)
		return;

	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto& dsUniforms = descriptorSetView->getUniforms();
	auto uniform = dsUniforms.find(name);
	if (uniform == dsUniforms.end())
		throw GardenError("Missing required descriptor set uniform. (" + string(name) + ")");

	auto& allocData = uniformData.at(name);
	#if GARDEN_DEBUG
	GARDEN_ASSERT(allocation < allocData.occupancy);
	for (auto freeAlloc : allocData.freeAllocs)
		GARDEN_ASSERT_MSG(allocation != freeAlloc.first, "Already freed allocation");
	#endif

	auto& resourceSet = uniform.value().resourceSets[0];
	resourceSet[allocation] = {};
	allocData.freeAllocs.emplace_back(allocation, frameIndex + (inFlightCount + 1));
}

void BindlessPool::flush(string_view name)
{
	GARDEN_ASSERT_MSG(descriptorSet, "Assert " + string(name));
	auto descriptorSetView = GraphicsAPI::get()->descriptorSetPool.get(descriptorSet);
	auto& allocData = uniformData.at(name);
	descriptorSetView->updateResources(name, allocData.occupancy);
}