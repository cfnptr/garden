// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("Init", ModelRenderEditorSystem::init);
}
void ModelRenderEditorSystem::init()
{
	EditorRenderSystem::getInstance()->registerEntityInspector<ModelRenderComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}

void ModelRenderEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto modelRenderView = Manager::getInstance()->get<ModelRenderComponent>(entity);
	auto editorSystem = EditorRenderSystem::getInstance();
	const auto lods = modelRenderView->getLods();
	auto lodCount = modelRenderView->getLodCount();

	if (lodCount > 0)
	{
		for (uint32 i = 0; i < lodCount; i++)
		{
			const auto& lod = lods[i];
			auto indexStr = to_string(i);
			ImGui::PushID(indexStr.c_str());
			ImGui::SeparatorText(indexStr.c_str());
			editorSystem->drawResource(lod.vertexBuffer, "Vertex Buffer");
			editorSystem->drawResource(lod.indexBuffer, "Index Buffer");
			ImGui::PopID();
		}
	}
	else
	{
		ImGui::TextDisabled("No levels of details");
	}

	if (ImGui::SmallButton(" + "))
		modelRenderView->addLod();
	ImGui::SameLine();
	if (ImGui::SmallButton(" - ") && modelRenderView->getLodCount() > 0)
		modelRenderView->removeLod();
}
#endif