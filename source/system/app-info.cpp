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

#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
AppInfoSystem::AppInfoSystem(string_view name, string_view nameLowercase, string_view description,
	string_view creator, string_view copyright, Version version,
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	const fs::path& cachePath, const fs::path& resourcesPath,
	#endif
	bool setSingleton) : Singleton(setSingleton), 
	name(name), nameLowercase(nameLowercase), description(description),
	creator(creator), copyright(copyright), version(version)
{
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	this->cachePath = cachePath;
	this->resourcesPath = resourcesPath;
	#endif
}
AppInfoSystem::~AppInfoSystem() { unsetSingleton(); }