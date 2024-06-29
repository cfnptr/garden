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

#include "garden/editor/system/ecs.hpp"

#if GARDEN_EDITOR

using namespace garden;

//**********************************************************************************************************************
EcsEditorSystem::EcsEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", EcsEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", EcsEditorSystem::deinit);
}
EcsEditorSystem::~EcsEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", EcsEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", EcsEditorSystem::deinit);
	}
}

void EcsEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());
	
	SUBSCRIBE_TO_EVENT("EditorRender", EcsEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
}
void EcsEditorSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", EcsEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderOrderedEvents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& orderedEvents = Manager::getInstance()->getOrderedEvents();
	for (auto orderedEvent : orderedEvents)
	{
		auto flags = (int)ImGuiTreeNodeFlags_OpenOnArrow;
		if (orderedEvent->subscribers.empty())
			flags |= (int)ImGuiTreeNodeFlags_Leaf;

		if (ImGui::TreeNodeEx(orderedEvent->name.c_str(), flags))
		{
			const auto& subscribers = orderedEvent->subscribers;
			for (const auto& subscriber : subscribers)
			{
				auto name = typeToString(subscriber.target_type());
				ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
			}
			ImGui::TreePop();
		}
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderUnorderedEvents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	auto manager = Manager::getInstance();
	const auto& events = manager->getEvents();
	const auto& orderedEvents = manager->getOrderedEvents();

	for (const auto& pair : events)
	{
		auto isFound = false;
		for (auto orderedEvent : orderedEvents)
		{
			if (pair.first == orderedEvent->name)
			{
				isFound = true;
				break;
			}
		}

		if (isFound)
			continue;

		const auto event = pair.second;

		auto flags = (int)ImGuiTreeNodeFlags_OpenOnArrow;
		if (event->subscribers.empty())
			flags |= (int)ImGuiTreeNodeFlags_Leaf;

		if (ImGui::TreeNodeEx(event->name.c_str(), flags))
		{
			const auto& subscribers = event->subscribers;
			for (const auto& subscriber : subscribers)
			{
				auto name = typeToString(subscriber.target_type());
				ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
			}
			ImGui::TreePop();
		}
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderRegisteredSystems()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& systems = Manager::getInstance()->getSystems();
	for (const auto& pair : systems)
	{
		auto name = typeToString(pair.first);
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

static void renderRegisteredComponents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& componentTypes = Manager::getInstance()->getComponentTypes();
	for (const auto& pair : componentTypes)
	{
		auto name = pair.second->getComponentName();
		if (name.empty())
			name = typeToString(pair.first);
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
void EcsEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	ImGui::SetNextWindowSize(ImVec2(320.0f, 256.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("ECS Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		if (ImGui::CollapsingHeader("Ordered Events"))
			renderOrderedEvents();
		if (ImGui::CollapsingHeader("Unordered Events"))
			renderUnorderedEvents();
		if (ImGui::CollapsingHeader("Registered Systems"))
			renderRegisteredSystems();
		if (ImGui::CollapsingHeader("Registered Components"))
			renderRegisteredComponents();
	}
	ImGui::End();
}

//**********************************************************************************************************************
void EcsEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("ECS Viewer"))
		showWindow = true;
}
#endif