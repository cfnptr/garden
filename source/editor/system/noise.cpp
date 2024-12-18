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

#include "garden/editor/system/noise.hpp"

#if GARDEN_EDITOR
namespace garden
{

class SimplexNoiseNode final : public ImGuiNode
{
	PinMap* pinMap = nullptr;
	ImNode::PinId outPinID = {};
public:
	SimplexNoiseNode(ImNode::PinId outPinID, PinMap* pinMap) :
		outPinID(outPinID), pinMap(pinMap)
	{
		pinMap->emplace(outPinID, this);
	}
	~SimplexNoiseNode() final
	{
		pinMap->erase(outPinID);
	}

	const string& getName() const final
	{
		static const std::string name = "Simplex Noise";
		return name;
	}
	void getOutPins(vector<ImGuiPinInfo>& pins) final
	{
		pins.push_back(ImGuiPinInfo::createOut(outPinID, "Noise"));
	}
};

} // namespace garden

using namespace garden;

//**********************************************************************************************************************
NoiseEditorSystem::NoiseEditorSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", NoiseEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", NoiseEditorSystem::deinit);

	ImNode::Config config;
	config.SettingsFile = "noise-editor.json";
	nodeEditor = ImNode::CreateEditor(&config);
}
NoiseEditorSystem::~NoiseEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ImNode::DestroyEditor((ImNode::EditorContext*)nodeEditor);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", NoiseEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", NoiseEditorSystem::deinit);
	}

	unsetSingleton();
}

void NoiseEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", NoiseEditorSystem::editorRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", NoiseEditorSystem::editorBarTool);
}
void NoiseEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", NoiseEditorSystem::editorRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", NoiseEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void NoiseEditorSystem::editorRender()
{
	if (!showWindow || !GraphicsSystem::Instance::get()->canRender())
		return;

	if (ImGui::Begin("Noise Editor", &showWindow, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		auto editorContext = (ImNode::EditorContext*)nodeEditor;
		ImNode::SetCurrentEditor(editorContext);
		ImNode::Begin("Noise Editor", ImVec2(0.0, 0.0f));

		if (ImNode::BeginCreate())
		{
			ImNode::PinId pinId = 0;
			if (ImNode::QueryNewNode(&pinId))
			{
				//newLinkPin = FindPin(pinId);
				//if (newLinkPin)
				//    showLabel("+ Create Node", ImColor(32, 45, 32, 180));

				if (ImNode::AcceptNewItem())
				{
					//createNewNode  = true;
					// newNodeLinkPin = FindPin(pinId);
					// newLinkPin = nullptr;
 					ImNode::Suspend();
 					ImGui::OpenPopup("Create New Node");
					ImNode::Resume();
				}
 			}
			ImNode::EndCreate();
		}

		ImNode::Suspend();
		if (ImNode::ShowBackgroundContextMenu())
			ImGui::OpenPopup("Create New Node");

		if (ImGui::BeginPopup("Create New Node"))
		{
			if (ImGui::MenuItem("Simplex Noise"))
				nodes.push_back(new SimplexNoiseNode(pinIdCounter++, &pinMap));
			ImGui::EndPopup();
		}
		ImNode::Resume();

		for (auto node : nodes)
		{
			ImNode::BeginNode(ImNode::NodeId(node));
			ImGui::Text(node->getName().c_str());

			node->getInPins(pins);
			if (ImGui::BeginChild("inPins"))
			{
				ImGuiPinInfo::renderPins(pins, true);
				ImGui::EndChild();
			} ImGui::SameLine();
			pins.clear();

			node->getOutPins(pins);
			if (ImGui::BeginChild("inPins"))
			{
				ImGuiPinInfo::renderPins(pins, false);
				ImGui::EndChild();
			}
			pins.clear();

			ImNode::EndNode();
		}

		ImNode::End();
		ImNode::SetCurrentEditor(nullptr);
	}
	ImGui::End();
}

void NoiseEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Noise Editor"))
		showWindow = true;
}
#endif