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

#include "garden/file.hpp"
#include "math/hex.hpp"

#include <fstream>
#include <random>

using namespace garden;

psize File::getFileSize(const fs::path& filePath)
{
	GARDEN_ASSERT(!filePath.empty());
	ifstream inputStream(filePath, ios::in | ios::binary | ios::ate);
	// Note: No need to set stream exception bits.

	if (!inputStream.is_open())
		throw GardenError("Failed to open binary file. (path: " + filePath.generic_string() + ")");
	return (psize)inputStream.tellg();
}

void File::loadBinary(const fs::path& filePath, vector<uint8>& data)
{
	GARDEN_ASSERT(!filePath.empty());
	ifstream inputStream(filePath, ios::in | ios::binary | ios::ate);
	// Note: No need to set stream exception bits.

	if (!inputStream.is_open())
		throw GardenError("Failed to open binary file. (path: " + filePath.generic_string() + ")");

	auto fileSize = (psize)inputStream.tellg();
	data.resize(fileSize);
	if (fileSize == 0)
		return;

	inputStream.seekg(0, ios::beg);
	if (!inputStream.read((char*)data.data(), fileSize))
		throw GardenError("Failed to read binary file. (path: " + filePath.generic_string() + ")");
}
bool File::tryLoadBinary(const fs::path& filePath, vector<uint8>& data)
{
	GARDEN_ASSERT(!filePath.empty());
	ifstream inputStream(filePath, ios::in | ios::binary | ios::ate);
	if (!inputStream.is_open())
		return false;

	auto fileSize = (psize)inputStream.tellg();
	data.resize(fileSize);
	if (fileSize == 0)
		return true;

	inputStream.seekg(0, ios::beg);
	if (!inputStream.read((char*)data.data(), fileSize))
		return false;

	return !inputStream.fail();
}

void File::loadBinary(const fs::path& filePath, uint8* data, psize size)
{
	GARDEN_ASSERT(!filePath.empty());
	ifstream inputStream(filePath, ios::in | ios::binary);
	// Note: No need to set stream exception bits.

	if (!inputStream.is_open())
		throw GardenError("Failed to open binary file. (path: " + filePath.generic_string() + ")");
	if (!inputStream.read((char*)data, size))
		throw GardenError("Failed to read binary file. (path: " + filePath.generic_string() + ")");
}
bool File::tryLoadBinary(const fs::path& filePath, uint8* data, psize size)
{
	GARDEN_ASSERT(!filePath.empty());
	ifstream inputStream(filePath, ios::in | ios::binary);
	if (!inputStream.is_open())
		return false;
	if (!inputStream.read((char*)data, size))
		return false;
	return !inputStream.fail();
}

//**********************************************************************************************************************
void File::storeBinary(const fs::path& filePath, const void* data, psize size)
{
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(size > 0);

	ofstream outputStream(filePath, ios::out | ios::binary);
	// Note: No need to set stream exception bits.

	if (!outputStream.is_open())
		throw GardenError("Failed to open binary file. (path: " + filePath.generic_string() + ")");
	if (!outputStream.write((char*)data, size))
		throw GardenError("Failed to write binary file. (path: " + filePath.generic_string() + ")");
}

fs::path File::createTmpName()
{
	auto randomDevice = random_device(); uint32 uuid[4];
	uuid[0] = randomDevice(); uuid[1] = randomDevice();
	uuid[2] = randomDevice(); uuid[3] = randomDevice();
	return toHex<uint32>(uuid, 4);
}

#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
bool File::tryGetResourcePath(const fs::path& appResourcesPath, const fs::path& resourcePath, fs::path& filePath)
{
	GARDEN_ASSERT(!resourcePath.empty());
	auto enginePath = GARDEN_RESOURCES_PATH / resourcePath;
	auto appPath = appResourcesPath / resourcePath;
	auto hasEngineFile = fs::exists(enginePath);
	auto hasAppFile = fs::exists(appPath);

	if ((hasEngineFile && hasAppFile) || (!hasEngineFile && !hasAppFile))
		return false;
	
	filePath = hasEngineFile ? enginePath : appPath;
	return true;
}
#endif