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

/***********************************************************************************************************************
 * @file
 * @brief Common message logging functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "logy/logger.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief Message logging system.
 * 
 * @details
 * A logging system records events, actions, and status messages that occur within a software application.
 * These logs provide a detailed record of activities and help developers, system administrators, and support 
 * teams diagnose and troubleshoot issues, monitor performance, and ensure the security of the system.
 */
class LogSystem final : public System
{
	logy::Logger logger;
	static LogSystem* instance;

	/**
	 * @brief Creates a new logging system instance.
	 * 
	 * @param level message logging level (log if <= level)
	 * @param rotationTime delay between log file rotation (0.0 = disabled)
	 */
	LogSystem(LogLevel level = ALL_LOG_LEVEL, double rotationTime = 0.0);
	/**
	 * @brief Destroys logging system instance.
	 */
	~LogSystem() final;
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Writes message to the log.
	 * 
	 * @param level logging level
	 * @param[in] message target logging message
	 */
	void log(LogLevel level, const string& message) noexcept;

	/**
	 * @brief Writes trace message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void trace(const string& message) noexcept  { log(TRACE_LOG_LEVEL, message); }
	/**
	 * @brief Writes debug message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void debug(const string& message) noexcept  { log(DEBUG_LOG_LEVEL, message); }
	/**
	 * @brief Writes information message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void info(const string& message) noexcept  { log(INFO_LOG_LEVEL, message); }
	/**
	 * @brief Writes warning message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void warn(const string& message) noexcept  { log(WARN_LOG_LEVEL, message); }
	/**
	 * @brief Writes error message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void error(const string& message) noexcept  { log(ERROR_LOG_LEVEL, message); }
	/**
	 * @brief Writes fatal message to the log. (MT-Safe)
	 * @param[in] message target logging message
	 */
	void fatal(const string& message) noexcept { log(FATAL_LOG_LEVEL, message); }

	/**
	 * @brief Returns current logger logging level. (MT-Safe)
	 * @details All messages above the current logging level will be skipped and not logged.
	 */
	LogLevel getLevel() const noexcept { return logger.getLevel(); }
	/**
	 * @brief Sets current logger logging level. (MT-Safe)
	 * @details All messages above the current logging level will be skipped and not logged.
	 */
	void setLevel(LogLevel level) noexcept { logger.setLevel(level); }

	/**
	 * @brief Returns current logger rotation delay time in seconds. (MT-Safe)
	 * 
	 * @details
	 * After the time expires, the current log file will be closed and 
	 * compressed, a new file stream for the log file will be created.
	 */
	double getRotationTime() const noexcept { return logger.getRotationTime(); }
	/**
	 * @brief Returns internal Logy logger instance. (MT-Safe)
	 * @details You can use it to get other logger information.
	 */
	const logy::Logger& getInternal() const noexcept { return logger; }

	/**
	 * @brief Returns logging system instance.
	 * @warning Do not use it if you have several logging system instances.
	 */
	static LogSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden