//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "logy/logger.hpp"

namespace garden
{

using namespace ecsm;

class LogSystem final : public System
{
private:
	logy::Logger logger;

	LogSystem(LogLevel level = ALL_LOG_LEVEL);
	~LogSystem() final;
	
	friend class ecsm::Manager;
	friend class LogEditor;
public:
	void log(LogLevel level, const string& message) noexcept
	{
		logger.log(level, "%.*s", message.length(), message.c_str());
	}

	void trace(const string& message) noexcept  { log(TRACE_LOG_LEVEL, message); }
	void debug(const string& message) noexcept  { log(DEBUG_LOG_LEVEL, message); }
	void info(const string& message) noexcept  { log(INFO_LOG_LEVEL, message); }
	void warn(const string& message) noexcept  { log(WARN_LOG_LEVEL, message); }
	void error(const string& message) noexcept  { log(ERROR_LOG_LEVEL, message); }
	void fatal(const string& message) noexcept { log(FATAL_LOG_LEVEL, message); }

	#if GARDEN_DEBUG
	static LogSystem* instance;
	#endif
};

} // garden