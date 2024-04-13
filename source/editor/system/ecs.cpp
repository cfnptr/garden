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
EcsEditorSystem::EcsEditorSystem(Manager* manager, EditorRenderSystem* system) : EditorSystem(manager, system)
{
	SUBSCRIBE_TO_EVENT("EditorRender", EcsEditorSystem::editorRender);
	SUBSCRIBE_TO_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
}
EcsEditorSystem::~EcsEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorRender", EcsEditorSystem::editorRender);
		UNSUBSCRIBE_FROM_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderOrderedEvents(Manager* manager)
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& orderedEvents = manager->getOrderedEvents();
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
				if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf))
					ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderUnorderedEvents(Manager* manager)
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

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
			for (auto& subscriber : subscribers)
			{
				auto name = typeToString(subscriber.target_type());
				if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf))
					ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderRegisteredSystems(Manager* manager)
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& systems = manager->getSystems();
	for (const auto& pair : systems)
	{
		auto name = typeToString(pair.first);
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf))
			ImGui::TreePop();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

static void renderRegisteredComponents(Manager* manager)
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& componentTypes = manager->getComponentTypes();
	for (const auto& pair : componentTypes)
	{
		auto name = pair.second->getComponentName();
		if (name.empty())
			name = typeToString(pair.first);
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf))
			ImGui::TreePop();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
void EcsEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::getInstance()->canRender())
		return;

	if (ImGui::Begin("ECS Viewer", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		auto manager = getManager();

		if (ImGui::CollapsingHeader("Ordered Events"))
			renderOrderedEvents(manager);
		if (ImGui::CollapsingHeader("Unordered Events"))
			renderUnorderedEvents(manager);
		if (ImGui::CollapsingHeader("Registered Systems"))
			renderRegisteredSystems(manager);
		if (ImGui::CollapsingHeader("Registered Components"))
			renderRegisteredComponents(manager);
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