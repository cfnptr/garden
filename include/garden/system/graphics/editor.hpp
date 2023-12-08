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
#include "garden/system/graphics.hpp"

#if GARDEN_EDITOR
#include "ecsm.hpp"
#include "math/aabb.hpp"
#include "garden/graphics/imgui.hpp"

#define DATA_SAMPLE_BUFFER_SIZE 512

namespace garden
{

using namespace ecsm;
using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
class EditorRenderSystem final : public System, public IRenderSystem
{
	vector<function<void()>> barTools;
	vector<function<void()>> barCreates;
	vector<function<void()>> barFiles;
	map<type_index, function<void(ID<Entity>)>> entityInspectors;
	float* cpuFpsBuffer = nullptr;
	float* gpuFpsBuffer = nullptr;
	float* cpuSortedBuffer = nullptr;
	float* gpuSortedBuffer = nullptr;
	void* hierarchyEditor = nullptr;
	void* resourceEditor = nullptr;
	string scenePath = "unnamed";
	int renderScaleType = 2;
	bool demoWindow = false;
	bool aboutWindow = false;
	bool optionsWindow = false;
	bool performanceStatistics = false;
	bool memoryStatistics = false;
	bool newScene = false;
	bool exportScene = false;

	~EditorRenderSystem();
	void initialize() final;
	void terminate() final;
	void render() final;

	void showMainMenuBar();
	void showAboutWindow();
	void showOptionsWindow();
	void showPerformanceStatistics();
	void showMemoryStatistics();
	void showEntityInspector();
	void showNewScene();
	void showExportScene();

	friend class ecsm::Manager;
	friend class HierarchyEditor;
public:
	uint32 opaqueDrawCount = 0, opaqueTotalCount = 0,
		translucentDrawCount = 0, translucentTotalCount = 0;
	Aabb selectedEntityAabb;
	ID<Entity> selectedEntity;

	void registerBarFile(function<void()> onBarFile) {
		barFiles.push_back(onBarFile); }
	void registerBarTool(function<void()> onBarTool) {
		barTools.push_back(onBarTool); }
	void registerBarCreate(function<void()> onBarCreate) {
		barCreates.push_back(onBarCreate); }

	void registerEntityInspector(type_index componentType,
		function<void(ID<Entity>)> onComponent)
	{
		auto result = entityInspectors.emplace(componentType, onComponent);
		#if GARDEN_DEBUG
		if (!result.second)
		{
			throw runtime_error("This component type is already registered. ("
				"name: " + string(componentType.name()) + ")");
		}
		#endif
	}
};

//--------------------------------------------------------------------------------------------------
static string toBinarySizeString(uint64 size)
{
	if (size > (uint64)(1024 * 1024 * 1024))
	{
		auto floatSize = (double)size / (double)(1024 * 1024 * 1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " GB";
	}
	if (size > (uint64)(1024 * 1024))
	{
		auto floatSize = (double)size / (double)(1024 * 1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " MB";
	}
	if (size > (uint64)(1024))
	{
		auto floatSize = (double)size / (double)(1024);
		return to_string((uint64)floatSize) + "." + to_string((uint64)(
			(double)(floatSize - (uint64)floatSize) * 10.0)) + " KB";
	}
	return to_string(size) + " B";
}

} // garden
#endif