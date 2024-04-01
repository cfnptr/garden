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

#include "garden/editor/system/log.hpp"
#include "mpio/os.hpp"
#include "mpmt/thread.hpp"

#if GARDEN_EDITOR

using namespace mpio;
using namespace mpmt;
using namespace garden;

//**********************************************************************************************************************
LogEditorSystem::LogEditorSystem(Manager* manager, LogSystem* system) : EditorSystem(manager, system)
{
	SUBSCRIBE_TO_EVENT("RenderEditor", LogEditorSystem::renderEditor);
	SUBSCRIBE_TO_EVENT("EditorBarTool", LogEditorSystem::editorBarTool);
}
LogEditorSystem::~LogEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("RenderEditor", LogEditorSystem::renderEditor);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", LogEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void updateTextBuffer(string& textBuffer,
	stringstream& logBuffer, string& logLine, const string& logSearch, bool searchCaseSensitive,
	bool includeFatal, bool includeError, bool includeWarn, bool includeInfo, bool includeDebug, bool includeTrace)
{
	textBuffer.clear();
	logBuffer.seekg(ios::beg);

	while (getline(logBuffer, logLine)) // TODO: async search for big logs
	{
		if (!logSearch.empty())
		{
			if (!find(logLine, logSearch, searchCaseSensitive))
				continue;
		}

		if ((includeFatal && logLine.find(logLevelToString(FATAL_LOG_LEVEL)) != string::npos) ||
			(includeError && logLine.find(logLevelToString(ERROR_LOG_LEVEL)) != string::npos) ||
			(includeWarn && logLine.find(logLevelToString(WARN_LOG_LEVEL)) != string::npos) ||
			(includeInfo && logLine.find(logLevelToString(INFO_LOG_LEVEL)) != string::npos) ||
			(includeDebug && logLine.find(logLevelToString(DEBUG_LOG_LEVEL)) != string::npos) ||
			(includeTrace && logLine.find(logLevelToString(TRACE_LOG_LEVEL)) != string::npos))
		{
			textBuffer += logLine;
			textBuffer += "\n";
		}
	}

	logBuffer.clear();
}

//**********************************************************************************************************************
void LogEditorSystem::renderEditor()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(560, 180), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Log Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		bufferMutex.lock();

		isDirty |= ImGui::Checkbox("Fatal", &includeFatal); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Error", &includeError); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Warn", &includeWarn); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Info", &includeInfo); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Debug", &includeDebug); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Trace", &includeTrace); ImGui::SameLine();

		bool isAll = includeFatal & includeError & includeWarn & includeInfo & includeDebug & includeTrace;
		if (ImGui::Checkbox("All", &isAll))
		{
			includeFatal = includeError = includeWarn = includeInfo = includeDebug = includeTrace = isAll;
			isDirty = true;
		}

		ImGui::SameLine();

		if (ImGui::Button("Clear"))
		{
			logBuffer.str("");
			isDirty = true;
		}

		ImGui::Spacing();

		if (isDirty)
		{
			updateTextBuffer(textBuffer, logBuffer, logLine, logSearch, searchCaseSensitive,
				includeFatal, includeError, includeWarn, includeInfo, includeDebug, includeTrace);
			isDirty = false;
		}

		ImGui::InputTextMultiline("##log", &textBuffer,
			ImVec2(-1, -(ImGui::GetFrameHeightWithSpacing() + 4)), ImGuiInputTextFlags_ReadOnly);
		ImGui::Spacing();

		isDirty |= ImGui::InputText("Search", &logSearch); ImGui::SameLine();
		isDirty |= ImGui::Checkbox("Aa", &searchCaseSensitive);

		bufferMutex.unlock();
	}
	ImGui::End();
}

//**********************************************************************************************************************
void LogEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Log Viewer"))
		showWindow = true;
}

//**********************************************************************************************************************
void LogEditorSystem::log(LogLevel level, const string& message)
{
	bufferMutex.lock();

	time_t rawTime;
	time(&rawTime);

#if __linux__ || __APPLE__
	struct tm timeInfo = *localtime(&rawTime);
#elif _WIN32
	struct tm timeInfo;
	if (gmtime_s(&timeInfo, &rawTime) != 0) abort();
#else
	#error Unknown operating system
#endif

	double clock = OS::getCurrentClock();
	int milliseconds = (int)((clock - floor(clock)) * 1000.0);

	char formattedTime[24];
	snprintf(formattedTime, 24, "%d-%02d-%02d %02d:%02d:%02d.%03d",
		timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
		timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec, milliseconds);

	char threadName[16];
	Thread::getName(threadName, 16);

	logBuffer << "[" << formattedTime << "] " << "[" << threadName << 
		"] [" << logLevelToString(level) << "]: " << message << "\n";
	isDirty = true;

	bufferMutex.unlock();
}
#endif