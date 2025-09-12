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

#include "garden/graphics/modelc.hpp"
#include "garden/thread-pool.hpp"

#include "assimp/Importer.hpp"
#include "assimp/DefaultLogger.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "zstd.h"

using namespace garden;
using namespace garden::graphics;

#if GARDEN_DEBUG || defined(EQUI2CUBE)
//******************************************************************************************************************
bool ModelConverter::convertModel(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{
	return false;
}
#endif

#if true
//******************************************************************************************************************
int main(int argc, char* argv[])
{
	return 0;
}
#endif