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
#include "garden/defines.hpp"
#include "math/vector.hpp"

namespace garden::graphics
{

using namespace std;
using namespace math;

//--------------------------------------------------------------------------------------------------
class Equi2Cube final
{
public:
	static void convert(const int3& coords, int32 cubemapSize, int2 equiSize,
		int2 equiSizeMinus1, const float4* equiPixels, float4* cubePixels, float invDim);

	#if GARDEN_DEBUG || defined(EQUI2CUBE)
	static bool convertImage(const fs::path& filePath,
		const fs::path& inputPath, const fs::path& outputPath);
	#endif
};

} // garden::graphics