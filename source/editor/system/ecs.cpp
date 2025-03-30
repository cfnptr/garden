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

#include "garden/editor/system/ecs.hpp"

#if GARDEN_EDITOR
using namespace garden;

//**********************************************************************************************************************
EcsEditorSystem::EcsEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", EcsEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", EcsEditorSystem::deinit);
}
EcsEditorSystem::~EcsEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", EcsEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", EcsEditorSystem::deinit);
	}
}

void EcsEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", EcsEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
}
void EcsEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", EcsEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", EcsEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void renderOrderedEvents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& orderedEvents = Manager::Instance::get()->getOrderedEvents();
	for (auto orderedEvent : orderedEvents)
	{
		auto flags = (int)ImGuiTreeNodeFlags_None;
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

	if (orderedEvents.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No registered ordered event");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderUnorderedEvents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	auto manager = Manager::Instance::get();
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

		auto flags = (int)ImGuiTreeNodeFlags_None;
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

	if (events.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No registered event");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
static void renderRegisteredSystems()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& systems = Manager::Instance::get()->getSystems();
	for (const auto& pair : systems)
	{
		auto name = typeToString(pair.first);
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	if (systems.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No registered system");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

static void renderRegisteredComponents()
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);

	const auto& componentTypes = Manager::Instance::get()->getComponentTypes();
	for (const auto& pair : componentTypes)
	{
		auto name = pair.second->getComponentName();
		if (name.empty())
			name = typeToString(pair.first);
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	if (componentTypes.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No registered component");
		ImGui::Unindent();
	}

	ImGui::PopStyleColor();
	ImGui::Spacing();
}

//**********************************************************************************************************************
void EcsEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::Instance::get()->canRender())
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