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

#include "garden/file.hpp"
#include <fstream>

using namespace garden;

//*********************************************************************************************************************
void File::loadBinary(const fs::path& filePath, vector<uint8>& data)
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

bool File::tryLoadBinary(const fs::path& filePath, vector<uint8>& data)
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

	return !inputStream.fail();
}

bool File::tryGetResourcePath(const fs::path& appResourcesPath, const fs::path& resourcePath, fs::path& filePath)
{
	auto enginePath = GARDEN_RESOURCES_PATH / resourcePath;
	auto appPath = appResourcesPath / resourcePath;
	auto hasEngineFile = fs::exists(enginePath);
	auto hasAppFile = fs::exists(appPath);

	if ((hasEngineFile && hasAppFile) || (!hasEngineFile && !hasAppFile))
		return false;
	
	filePath = hasEngineFile ? enginePath : appPath;
	return true;
}