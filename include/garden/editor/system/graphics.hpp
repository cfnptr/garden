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
namespace garden
{

class GraphicsEditorSystem final : public EditorSystem<GraphicsSystem>
{
	float* cpuFpsBuffer = nullptr;
	float* gpuFpsBuffer = nullptr;
	float* cpuSortedBuffer = nullptr;
	float* gpuSortedBuffer = nullptr;
	bool performanceStatistics = false;
	bool memoryStatistics = false;

	GraphicsEditorSystem(Manager* manager, GraphicsSystem* graphicsSystem);
	~GraphicsEditorSystem() final;

	void renderEditor();
	void editorBarTool();

	void showPerformanceStatistics();
	void showMemoryStatistics();

	friend class ecsm::Manager;
public:
	uint32 opaqueDrawCount = 0;
	uint32 opaqueTotalCount = 0;
	uint32 translucentDrawCount = 0;
	uint32 translucentTotalCount = 0;
};

} // namespace garden
#endif