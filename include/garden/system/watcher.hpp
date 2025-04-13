//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

// TODO: refactor this

/*
#pragma once
#include "garden/system/log.hpp"
#include <functional>

namespace garden
{

//--------------------------------------------------------------------------------------------------
class WatcherSystem final : public System
{
public:
	using OnChange = std::function<void(const string& path)>;
	using Listeners = map<string, vector<OnChange>, les<>>;
private:
	void* instance = nullptr;
	vector<string> changedFiles;
	Listeners listeners;
	mutex locker = {};

	void initialize() final;
	void terminate() final;
	void update() final;
	friend class ecsm::Manager;
public:
	vector<string>& getChangedFiles() noexcept { return changedFiles; }
	mutex& getLocker() noexcept { return locker; }

	void addListener(string_view extension, const OnChange& onChange) {
		listeners[extension].push_back(onChange); }
};

} // namespace garden
*/