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
	SUBSCRIBE_TO_EVENT("Init", AnimationEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", AnimationEditorSystem::deinit);
}
AnimationEditorSystem::~AnimationEditorSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", AnimationEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", AnimationEditorSystem::deinit);
	}
}

void AnimationEditorSystem::init()
{
	SUBSCRIBE_TO_EVENT("EditorRender", AnimationEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", AnimationEditorSystem::editorBarTool);

	EditorRenderSystem::get()->registerEntityInspector<AnimationComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void AnimationEditorSystem::deinit()
{
	EditorRenderSystem::get()->unregisterEntityInspector<AnimationComponent>();

	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", AnimationEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", AnimationEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void AnimationEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::get()->canRender())
		return;

	if (ImGui::Begin("Animation Edtor", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		ImGui::Text("It's not implemented yet :(");
		ImGui::Text("But you can be the one who will do it!");
	}
	ImGui::End();
}
void AnimationEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Animation Editor"))
		showWindow = true;
}

//**********************************************************************************************************************
static void renderAnimationSelector(ID<Entity> entity)
{
	static const set<string> extensions = { ".anim" };
	EditorRenderSystem::get()->openFileSelector([entity](const fs::path& selectedFile)
	{
		auto animationView = AnimationSystem::get()->tryGet(entity);
		if (!animationView || EditorRenderSystem::get()->selectedEntity != entity)
			return;

		auto path = selectedFile;
		path.replace_extension();
		auto animation = ResourceSystem::get()->loadAnimation(path, true);
		if (animation)
			animationView->emplaceAnimation(path.generic_string(), std::move(animation));
	},
	AppInfoSystem::get()->getResourcesPath() / "animations", extensions);
}

void AnimationEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto animationView = AnimationSystem::get()->get(entity);
		ImGui::Text("Playing: %s, Frame: %f", animationView->isPlaying ?
			animationView->active.c_str() : "none", animationView->frame);
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto animationView = AnimationSystem::get()->get(entity);
	if (ImGui::Checkbox("Playing", &animationView->isPlaying))
	{
		bool isLooped;
		if (animationView->isPlaying && animationView->getActiveLooped(isLooped))
		{
			if (!isLooped)
				animationView->frame = 0.0f;
		}
	}
	
	ImGui::SameLine();
	ImGui::Checkbox("Randomize Start", &animationView->randomizeStart);

	ImGui::InputText("Active", &animationView->active); // TODO: dropdown selector
	if (ImGui::BeginPopupContextItem("active"))
	{
		if (ImGui::MenuItem("Reset Default"))
			animationView->active = "";
		ImGui::EndPopup();
	}

	ImGui::DragFloat("Frame", &animationView->frame);
	if (ImGui::BeginPopupContextItem("frame"))
	{
		if (ImGui::MenuItem("Reset Default"))
			animationView->frame = 0.0f;
		ImGui::EndPopup();
	}
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Animations"))
	{
		ImGui::Indent();
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		auto resourceSystem = ResourceSystem::get();
		auto& animations = animationView->getAnimations();

		for (auto i = animations.begin(); i != animations.end(); i++)
		{
			ImGui::InputText("Path", (string*)&i->first, ImGuiInputTextFlags_ReadOnly); ImGui::SameLine();
			if (ImGui::Button(" - "))
			{
				resourceSystem->destroyShared(i->second);
				i = animationView->eraseAnimation(i);
				if (i == animations.end())
					break;
			}
		}

		if (ImGui::Button("Add Animation", ImVec2(-FLT_MIN, 0.0f)))
			renderAnimationSelector(entity);

		ImGui::PopStyleColor();
		ImGui::Unindent();
	}
}
#endif