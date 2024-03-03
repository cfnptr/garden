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

// TODO: refactor this

/*
#pragma once
#include "garden/system/log.hpp"
#include <functional>

namespace garden
{

using namespace ecsm;

//--------------------------------------------------------------------------------------------------
class WatcherSystem final : public System
{
public:
	using OnChange = function<void(const string&)>;
private:
	void* instance = nullptr;
	vector<string> changedFiles;
	map<string, vector<OnChange>> listeners;
	mutex locker = {};

	void initialize() final;
	void terminate() final;
	void update() final;
	friend class ecsm::Manager;
public:
	vector<string>& getChangedFiles() noexcept { return changedFiles; }
	mutex& getLocker() noexcept { return locker; }

	void addListener(const string& extension, const OnChange& onChange) {
		listeners[extension].push_back(onChange); }
};

} // namespace garden
*/