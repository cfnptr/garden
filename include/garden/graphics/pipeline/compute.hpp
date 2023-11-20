//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/graphics/pipeline.hpp"

namespace garden::graphics
{

using namespace std;
using namespace math;
using namespace ecsm;
class ComputePipelineExt;

//--------------------------------------------------------------------------------------------------
class ComputePipeline final : public Pipeline
{
public:
	struct ComputeCreateData : public CreateData
	{
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
		int3 localSize = int3(0);
		vector<uint8> code;
	};
private:
	uint8 _alignment = 0;
	int3 localSize = int3(0);

	ComputePipeline() = default;
	ComputePipeline(const fs::path& path,
		uint32 maxBindlessCount, bool useAsync, uint64 pipelineVersion) :
		Pipeline(PipelineType::Compute, path, maxBindlessCount, useAsync, pipelineVersion) { }
	ComputePipeline(ComputeCreateData& createData, bool useAsync);

	friend class CommandBuffer;
	friend class ComputePipelineExt;
	friend class LinearPool<ComputePipeline>;
public:
	const int3& getLocalSize() const noexcept { return localSize; }

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	void dispatch(const int3& count, bool isGlobalCount = true);
};

//--------------------------------------------------------------------------------------------------
class ComputePipelineExt final
{
public:
	static int3& getLocalSize(ComputePipeline& pipeline)
		noexcept { return pipeline.localSize; }

	static ComputePipeline create(
		ComputePipeline::ComputeCreateData& createData, bool useAsync)
	{
		return ComputePipeline(createData, useAsync);
	}
	static void moveInternalObjects(ComputePipeline& source,
		ComputePipeline& destination) noexcept
	{
		ComputePipelineExt::getLocalSize(destination) =
			ComputePipelineExt::getLocalSize(source);
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // garden::graphics