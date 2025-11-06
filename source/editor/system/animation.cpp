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

#include "garden/editor/system/animation.hpp"

#if GARDEN_EDITOR
#include "garden/system/animation.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/app-info.hpp"

using namespace garden;

//**********************************************************************************************************************
AnimationEditorSystem::AnimationEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", AnimationEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", AnimationEditorSystem::deinit);
}
AnimationEditorSystem::~AnimationEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", AnimationEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", AnimationEditorSystem::deinit);
	}
}

void AnimationEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", AnimationEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", AnimationEditorSystem::editorBarTool);

	EditorRenderSystem::Instance::get()->registerEntityInspector<AnimationComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void AnimationEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		EditorRenderSystem::Instance::get()->unregisterEntityInspector<AnimationComponent>();

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", AnimationEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", AnimationEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void AnimationEditorSystem::preUiRender()
{
	if (!showWindow)
		return;

	if (ImGui::Begin("Animation Editor", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
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
	static const vector<string_view> extensions = { ".anim" };
	EditorRenderSystem::Instance::get()->openFileSelector([entity](const fs::path& selectedFile)
	{
		auto animationView = Manager::Instance::get()->tryGet<AnimationComponent>(entity);
		if (!animationView || EditorRenderSystem::Instance::get()->selectedEntity != entity)
			return;

		auto path = selectedFile;
		path.replace_extension();
		auto animation = ResourceSystem::Instance::get()->loadAnimation(path, true);
		if (animation)
			animationView->emplaceAnimation(path.generic_string(), std::move(animation));
	},
	AppInfoSystem::Instance::get()->getResourcesPath() / "animations", extensions);
}

void AnimationEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto animationView = Manager::Instance::get()->get<AnimationComponent>(entity);
		ImGui::Text("Playing: %s, Frame: %f", animationView->isPlaying ?
			animationView->active.c_str() : "none", animationView->frame);
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto animationView = Manager::Instance::get()->get<AnimationComponent>(entity);
	if (ImGui::Checkbox("Playing", &animationView->isPlaying))
	{
		bool isLooped;
		if (animationView->isPlaying && animationView->getActiveLooped(isLooped) && !isLooped)
			animationView->frame = 0.0f;
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
	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("AnimationPath");
		if (payload)
			animationView->active = string((const char*)payload->Data, payload->DataSize);
		ImGui::EndDragDropTarget();
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
		auto resourceSystem = ResourceSystem::Instance::get();
		auto& animations = animationView->getAnimations();

		if (ImGui::BeginPopupContextItem("animations"))
		{
			if (ImGui::MenuItem("Remove All Animation"))
			{
				for (auto& pair : animations)
					resourceSystem->destroyShared(pair.second);
				animationView->clearAnimations();
			}
			ImGui::EndPopup();
		}

		ImGui::Indent();
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

		for (auto i = animations.begin(); i != animations.end(); i++)
		{
			ImGui::TreeNodeEx(i->first.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
			if (ImGui::BeginPopupContextItem(i->first.c_str()))
			{
				if (ImGui::MenuItem("Set As Active"))
					animationView->active = i->first;
				if (ImGui::MenuItem("Copy Animation Path"))
					ImGui::SetClipboardText(i->first.c_str());

				if (ImGui::MenuItem("Reload Animation"))
				{
					auto animationPath = i->first;
					resourceSystem->destroyShared(i->second); animationView->eraseAnimation(i);
					auto newAnimation = resourceSystem->loadAnimation(animationPath, true);
					animationView->emplaceAnimation(std::move(animationPath), std::move(newAnimation));
					ImGui::EndPopup();
					break;
				}
				if (ImGui::MenuItem("Remove Animation"))
				{
					resourceSystem->destroyShared(i->second);
					i = animationView->eraseAnimation(i);

					if (i == animations.end())
					{
						ImGui::EndPopup();
						break;
					}
				}
				ImGui::EndPopup();
			}
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("AnimationPath", i->first.c_str(), i->first.length());
				ImGui::Text("%s", i->first.c_str());
				ImGui::EndDragDropSource();
			}
		}

		if (ImGui::Button("Add Animation", ImVec2(-FLT_MIN, 0.0f)))
			renderAnimationSelector(entity);

		ImGui::PopStyleColor();
		ImGui::Unindent();
	}
}
#endif