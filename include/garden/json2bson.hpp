// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

/***********************************************************************************************************************
 * @file
 * @brief Common JSON to binary JSON conversion functions.
 */

#pragma once
#include "garden/defines.hpp"

namespace garden
{

/**
 * @brief JSON to binary JSON converter. 
 */
class Json2Bson final
{
public:
	#if GARDEN_DEBUG || defined(JSON2BSON)
	/**
	 * @brief Converts input JSON file to binary JSON. 
	 * 
	 * @param filePath target JSON to convert path
	 * @param inputPath input JSON directory path
	 * @param outputPath output BSON directory path
	 * 
	 * @return Returns false if failed to open JSON file.
	 * @throw GardenError on JSON conversion error.
	 */
	static bool convertFile(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath);
	#endif
};

} // namespace garden