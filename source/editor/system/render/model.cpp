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

#include "garden/editor/system/render/model.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/model.hpp"

using namespace garden;

//**********************************************************************************************************************
ModelRenderEditorSystem::ModelRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", ModelRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ModelRenderEditorSystem::deinit);
}
ModelRenderEditorSystem::~ModelRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ModelRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ModelRenderEditorSystem::deinit);
	}
}

void ModelRenderEditorSystem::init()
{
	EditorRenderSystem::Instance::get()->registerEntityInspector<ModelRenderComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void ModelRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<ModelRenderComponent>();
}

void ModelRenderEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto modelView = ModelRenderSystem::Instance::get()->getComponent(entity);
	auto editorSystem = EditorRenderSystem::Instance::get();
	auto& levels = modelView->levels;

	if (!levels.empty())
	{
		for (uint32 i = 0; i < (uint32)levels.size(); i++)
		{
			auto& level = levels[i];
			auto indexStr = to_string(i);
			ImGui::PushID(indexStr.c_str());
			ImGui::SeparatorText(indexStr.c_str());
			editorSystem->drawResource(level.vertexBuffer, "Vertex Buffer");
			editorSystem->drawResource(level.indexBuffer, "Index Buffer");
			ImGui::PopID();
		}
	}
	else
	{
		ImGui::TextDisabled("No levels of details");
	}

	if (ImGui::SmallButton(" + "))
		levels.push_back({});
	ImGui::SameLine();
	if (ImGui::SmallButton(" - ") && !levels.empty())
		levels.resize(levels.size() - 1);
}
#endif