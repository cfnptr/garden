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

#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
AppInfoSystem::AppInfoSystem(const string& name, const string& nameLowercase, const string& description,
	const string& creator, const string& copyright, Version version,
	#if GARDEN_DEBUG
	fs::path cachesPath, fs::path resourcesPath,
	#endif
	bool setSingleton) : Singleton(setSingleton)
{
	this->name = name;
	this->nameLowercase = nameLowercase;
	this->description = description;
	this->creator = creator;
	this->copyright = copyright;
	this->version = version;

	#if GARDEN_DEBUG
	this->cachesPath = cachesPath;
	this->resourcesPath = resourcesPath;
	#endif
}
AppInfoSystem::~AppInfoSystem() { unsetSingleton(); }