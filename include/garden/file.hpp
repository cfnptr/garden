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

/**********************************************************************************************************************
 * @file
 * @brief Common file system functions.
 */

#pragma once
#include "garden/defines.hpp"

#include <vector>
#include <fstream>

namespace garden
{

using namespace std;
using namespace math;

/**
 * @brief Loads binary data from the file.
 * 
 * @param[in] filePath target file path
 * @param[out] data binary data buffer
 * 
 * @throw runtime_error if failed to load file data.
 */
static void loadBinaryFile(const fs::path& filePath, vector<uint8>& data)
{
	ifstream inputStream(filePath, ios::in | ios::binary | ios::ate);
	inputStream.exceptions(ios::failbit | ios::badbit);

	if (!inputStream.is_open())
		throw runtime_error("Failed to open binary file. (path: " + filePath.generic_string() + ")");

	auto fileSize = (psize)inputStream.tellg();
	if (fileSize == 0)
		throw runtime_error("No binary file data. (path: " + filePath.generic_string() + ")");

	inputStream.seekg(0, ios::beg);
	data.resize(fileSize);

	if (!inputStream.read((char*)data.data(), fileSize))
		throw runtime_error("Failed to read binary file. (path: " + filePath.generic_string() + ")");
}

/**
 * @brief Loads binary data from the file.
 * 
 * @param[in] filePath target file path
 * @param[out] data binary data buffer
 * 
 * @return True on success, otherwise false.
 */
static bool tryLoadBinaryFile(const fs::path& filePath, vector<uint8>& data)
{
	ifstream inputStream(filePath, ios::in | ios::binary | ios::ate);
	if (!inputStream.is_open())
		return false;

	auto fileSize = (psize)inputStream.tellg();
	if (fileSize == 0)
		return false;

	inputStream.seekg(0, ios::beg);
	data.resize(fileSize);
	if (!inputStream.read((char*)data.data(), fileSize))
		return false;
	return true;
}

/**********************************************************************************************************************
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