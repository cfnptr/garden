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

#pragma once
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "garden/system/log.hpp"

namespace garden
{

class LogEditorSystem final : public EditorSystem<LogSystem>
{
	string logSearch;
	string textBuffer, logLine;
	stringstream logBuffer;
	mutex bufferMutex;
	bool showWindow = false;
	bool searchCaseSensitive = false;
	bool includeFatal = true;
	bool includeError = true;
	bool includeWarn = true;
	bool includeInfo = true;
	bool includeDebug = true;
	bool includeTrace = true;
	bool includeAll = true;
	bool isDirty = true;

	LogEditorSystem(Manager* manager, LogSystem* system);
	~LogEditorSystem() final;

	void renderEditor();
	void editorBarTool();

	friend class ecsm::Manager;
public:
	void log(LogLevel level, const string& message);
};

} // namespace garden
#endif