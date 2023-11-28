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
#include "math/types.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

extern "C"
{
#include "mpmt/thread.h"
};

// TODO: move this to logy repo

namespace garden
{

#if _WIN32
#define ANSI_NAME_COLOR ""
#define ANSI_RESET_COLOR ""
#else
#define ANSI_NAME_COLOR "\e[0;90m"
#define ANSI_RESET_COLOR "\e[0m"
#endif

using namespace math;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
class LogSystem final : public System
{
public:
	enum class Severity : uint8
	{
		Off, Fatal, Error, Warn, Info, Debug, Trace, All, Count
	};

private:
	mutex writeMutex = {};
	ofstream fileStream;
	Severity severity = {};

	LogSystem(Severity severity = Severity::All);
	~LogSystem() final;
	
	friend class ecsm::Manager;
	friend class LogEditor;
public:
//--------------------------------------------------------------------------------------------------
	static string_view severityToString(Severity severity)
	{
		GARDEN_ASSERT((uint8)severity < (uint8)Severity::Count);
		static const string_view names[(uint8)Severity::Count] =
		{ "OFF", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE", "ALL" };
		return names[(psize)severity];
	}

	Severity getSeverity() const noexcept { return severity; }

	template<class T = string>
	void log(Severity severity, const T& message)
	{
		if (severity > this->severity) return;
		
		lock_guard lock(writeMutex);
		auto now = chrono::system_clock::now();
		auto time = chrono::system_clock::to_time_t(now);
		auto tm = gmtime(&time);
		auto ms =
			chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count() - 
			chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count() * 1000;
		char threadName[16];
		getThreadName(threadName, 16);
		fileStream.fill('0');
		fileStream << "[" << tm->tm_year + 1900 << "-" << setw(2) << tm->tm_mon + 1 <<
			"-" << setw(2) << tm->tm_mday << " " << setw(2) << tm->tm_hour << ":" <<
			setw(2) << tm->tm_min << ":" << setw(2) << tm->tm_sec << "." << setw(3) << 
			(int)ms << "] [" << threadName << "] [" <<
			severityToString(severity) << "]: " << message << "\n";
		fileStream.flush();

		#if GARDEN_DEBUG
		const char* color;

		#if _WIN32
		color = "";
		#else
		switch (severity)
		{
		default: color = "\e[0;37m"; break;
		case LogSystem::Severity::Fatal: color = "\e[0;31m"; break;
		case LogSystem::Severity::Error: color = "\e[0;91m"; break;
		case LogSystem::Severity::Warn: color = "\e[0;93m"; break;
		case LogSystem::Severity::Debug: color = "\e[0;92m"; break;
		case LogSystem::Severity::Trace: color = "\e[0;94m"; break;
		}
		#endif

		cout.fill('0');
		cout << "[" ANSI_NAME_COLOR << tm->tm_year + 1900 << "-" << setw(2) <<
			tm->tm_mon + 1 << "-" << setw(2) << tm->tm_mday << " " << setw(2) <<
			tm->tm_hour << ":" << setw(2) << tm->tm_min << ":" << setw(2) <<
			tm->tm_sec << "." << setw(3) << (int)ms << ANSI_RESET_COLOR "] ["
			ANSI_NAME_COLOR << threadName << ANSI_RESET_COLOR "] [" <<
			color << severityToString(severity) << ANSI_RESET_COLOR "]: " <<
			message << "\n";
		#endif
	}

//--------------------------------------------------------------------------------------------------
	template<class T = string>
	void trace(const T& message) { log(Severity::Trace, message); }
	template<class T = string>
	void debug(const T& message) { log(Severity::Debug, message); }
	template<class T = string>
	void info(const T& message) { log(Severity::Info, message); }
	template<class T = string>
	void warn(const T& message) { log(Severity::Warn, message); }
	template<class T = string>
	void error(const T& message) { log(Severity::Error, message); }
	template<class T = string>
	void fatal(const T& message) { log(Severity::Fatal, message); }
};

} // garden