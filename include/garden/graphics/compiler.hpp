//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/system/log.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

#ifndef GSL_COMPILER
#include "pack/reader.hpp"
#endif

#include <map>
#include <sstream>

namespace garden::graphics
{

using namespace std;

//--------------------------------------------------------------------------------------------------
class Compiler final
{
public:
	struct GraphicsData : public GraphicsPipeline::GraphicsCreateData
	{
		#ifndef GSL_COMPILER
		pack::Reader* packReader = nullptr;
		int32 threadIndex = 0;
		#endif
	};
	struct ComputeData : public ComputePipeline::ComputeCreateData
	{
		#ifndef GSL_COMPILER
		pack::Reader* packReader = nullptr;
		int32 threadIndex = 0;
		#endif
	};

	#if GARDEN_DEBUG || defined(GSL_COMPILER)
	static bool compileGraphicsShaders(
		const fs::path& inputPath, const fs::path& outputPath,
		const vector<fs::path>& includePaths, GraphicsData& data);
	static bool compileComputeShader(
		const fs::path& inputPath, const fs::path& outputPath,
		const vector<fs::path>& includePaths, ComputeData& data);
	#endif
	
	static void loadGraphicsShaders(GraphicsData& data);
	static void loadComputeShader(ComputeData& data);
};

} // namespace garden::graphics