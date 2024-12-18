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

/***********************************************************************************************************************
 * @file
 * @brief Common immediate GUI node editor functions. (ImGui)
 */

#pragma once
#include "garden/nodes.hpp"
#include "garden/graphics/imgui.hpp"
#include "imgui_node_editor.h"

#include <map>

namespace garden
{

namespace ImNode = ax::NodeEditor;

/**
 * @brief ImGui node pin information container.
 */
struct ImGuiPinInfo final
{
	string text;
	ImNode::PinId id ={};
	ImNode::PinKind kind = {};

	/**
	 * @brief Creates a new empty ImGui node pin information container.
	 */
	ImGuiPinInfo() noexcept { }
	/**
	 * @brief Creates a new ImGui node pin information container.
	 */
	ImGuiPinInfo(ImNode::PinId id, ImNode::PinKind kind, const string& text = "") noexcept :
		id(id), kind(kind), text(text) {}

	/**
	 * @brief Creates a new ImGui node input pin information container.
	 */
	static ImGuiPinInfo createIn(ImNode::PinId id, const string& text = "") noexcept
	{
		return ImGuiPinInfo(id, ImNode::PinKind::Input, text);
	}
	/**
	 * @brief Creates a new ImGui node output pin information container.
	 */
	static ImGuiPinInfo createOut(ImNode::PinId id, const string& text = "") noexcept
	{
		return ImGuiPinInfo(id, ImNode::PinKind::Output, text);
	}

	/**
	 * @brief Renders ImGui node pins.
	 * @param[in] pins target node pin array
	 */
	static void renderPins(const vector<ImGuiPinInfo>& pins, bool isInput)
	{
		auto fmt = isInput ? "-> %s" : "%s ->";
		for (auto& pin : pins)
		{
			ImNode::BeginPin(pin.id, pin.kind);
			ImGui::Text(fmt, pin.text.c_str());
			ImNode::EndPin();
		}
	}
};

/**
 * @brief ImGui pin ID less comparator.
 */
struct ImGuiPinIdLess
{
    bool operator()(const ImNode::PinId& lhs, const ImNode::PinId& rhs) const
    {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};

/***********************************************************************************************************************
 * @brief Base ImGui node container.
 */
class ImGuiNode
{
public:
	using PinMap = map<ImNode::PinId, ImGuiNode*, ImGuiPinIdLess>;

	/**
	 * @brief Destroys ImGui node instance and it resources.
	 */
	virtual ~ImGuiNode() { }

	/**
	 * @brief Returns ImGui node name string.
	 */
	virtual const string& getName() const
	{
		static const std::string name = "";
		return name;
	} 
	/**
	 * @brief Returns all ImGui node input pins.
	 * @param[out] pins all node in pins array
	 */
	virtual void getInPins(vector<ImGuiPinInfo>& pins) { }
	/**
	 * @brief Returns all ImGui node output pins.
	 * @param[out] pins all node out pins array
	 */
	virtual void getOutPins(vector<ImGuiPinInfo>& pins) { }

	/**
	 * @brief Evaluates ImGui node value.
	 * 
	 * @param[in,out] value input node value
	 * @param[in,out] type input value type
	 * @return True if evaluation was successful.
	 */
	virtual bool evaluate() { return false; }

	/**
	 * @brief Returns ImGui node value.
	 */
	virtual void* getValue() { return nullptr; }
	/**
	 * @brief Returns ImGui node value type.
	 */
	virtual uint32 getValueType() { return (uint32)NodeValueType::None; }
};

class OperatorImGuiNode final : ImGuiNode
{
	PinMap* pinMap = nullptr;
	NodeValueNumber value = {};
public:
	ImNode::PinId leftInPin = {};
	ImNode::PinId rightInPin = {};
	ImNode::PinId outPin = {};
	NodeOperatorType type = {};

	OperatorImGuiNode(NodeOperatorType type, PinMap* pinMap) : type(type), pinMap(pinMap) { }
	bool evaluate() final;
};

}; // namespace garden