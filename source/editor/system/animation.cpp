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

#include "garden/editor/system/animation.hpp"

#if GARDEN_EDITOR
#include "garden/system/animation.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
AnimationEditorSystem::AnimationEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", AnimationEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", AnimationEditorSystem::deinit);
}
AnimationEditorSystem::~AnimationEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", AnimationEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", AnimationEditorSystem::deinit);
	}
}

void AnimationEditorSystem::init()
{
	GARDEN_ASSERT(Manager::getInstance()->has<EditorRenderSystem>());
	EditorRenderSystem::getInstance()->registerEntityInspector<AnimationComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void AnimationEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<AnimationComponent>();
}

//**********************************************************************************************************************
static void renderAnimationSelector(ID<Entity> entity)
{
	static const set<string> extensions = { ".anim" };
	EditorRenderSystem::getInstance()->openFileSelector([entity](const fs::path& selectedFile)
	{
		auto animationComponent = Manager::getInstance()->tryGet<AnimationComponent>(entity);
		if (!animationComponent || EditorRenderSystem::getInstance()->selectedEntity != entity)
			return;

		auto path = selectedFile;
		path.replace_extension();
		auto animation = ResourceSystem::getInstance()->loadAnimation(path, true);
		if (animation)
			animationComponent->emplaceAnimation(path.generic_string(), std::move(animation));
	},
	AppInfoSystem::getInstance()->getResourcesPath() / "animations", extensions);
}

void AnimationEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (!isOpened)
		return;

	auto animationComponent = Manager::getInstance()->get<AnimationComponent>(entity);
	ImGui::Checkbox("Playing", &animationComponent->isPlaying);
	ImGui::InputText("Active", &animationComponent->active); // TODO: dropdown selector
	ImGui::DragFloat("Frame", &animationComponent->frame);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Animations"))
	{
		ImGui::Indent();
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto animationSystem = AnimationSystem::getInstance();
		auto& animations = animationComponent->getAnimations();

		for (auto i = animations.begin(); i != animations.end(); i++)
		{
			ImGui::InputText("Path", (string*)&i->first, ImGuiInputTextFlags_ReadOnly); ImGui::SameLine();
			if (ImGui::Button(" - "))
			{
				animationSystem->destroy(i->second);
				i = animationComponent->eraseAnimation(i);
			}
		}

		if (ImGui::Button("Add Animation", ImVec2(-FLT_MIN, 0.0f)))
			renderAnimationSelector(entity);

		ImGui::PopStyleColor();
		ImGui::Unindent();
	}
}
#endif