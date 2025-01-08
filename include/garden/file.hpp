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

/**********************************************************************************************************************
 * @file
 * @brief Common file system functions.
 */

#pragma once
#include "garden/defines.hpp"
#include <vector>

namespace garden
{

class File final
{
public:
	/**
	 * @brief Loads binary data from the file.
	 * 
	 * @param[in] filePath target file path
	 * @param[out] data binary data buffer
	 * 
	 * @throw GardenError if failed to load file data.
	 */
	static void loadBinary(const fs::path& filePath, vector<uint8>& data);
	/**
	 * @brief Loads binary data from the file.
	 * 
	 * @param[in] filePath target file path
	 * @param[out] data binary data buffer
	 * 
	 * @return True on success, otherwise false.
	 */
	static bool tryLoadBinary(const fs::path& filePath, vector<uint8>& data);

	#if GARDEN_DEBUG
	/******************************************************************************************************************
	 * @brief Returns resource file path in the system. (Debug Only)
	 * @details The resource is searched inside the engine resources folder and the application resources folder.
	 * 
	 * @param[in] appResourcesPath application resources directory path
	 * @param[in] resourcePath target resource path
	 * @param[out] filePath reference to the found resource file path
	 * 
	 * @return True if resource is found, false if not found or file is ambiguous.
	 */
	static bool tryGetResourcePath(const fs::path& appResourcesPath, const fs::path& resourcePath, fs::path& filePath);
	#endif
};

/**
 * @brief Converts binary size to the string representation. (KB, MB, GB)
 * @param size target binary size
 */
static string toBinarySizeString(uint64 size)
{
	if (size > (uint64)(1024 * 1024 * 1024))
	{
		auto floatSize = (double)size / (double)(1024 * 1024 * 1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " GB";
	}
	if (size > (uint64)(1024 * 1024))
	{
		auto floatSize = (double)size / (double)(1024 * 1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " MB";
	}
	if (size > (uint64)(1024))
	{
		auto floatSize = (double)size / (double)(1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " KB";
	}
	return to_string(size) + " B";
}

} // namespace garden