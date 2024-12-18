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

#include "garden/graphics/imgui-nodes.hpp"

using namespace garden;

//**********************************************************************************************************************
template<NodeValueType TV>
static NodeValueNumber convertValueNumber(NodeValueNumber number) noexcept
{
	NodeValueNumber result;
	result.base.type = (uint32)TV;
	if constexpr (TV == NodeValueType::Uint32)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Int32: result.u32.value = (uint32)number.i32.value; break;
		case (uint32)NodeValueType::Uint64: result.u32.value = (uint32)number.u64.value; break;
		case (uint32)NodeValueType::Int64: result.u32.value = (uint32)number.i64.value; break;
		case (uint32)NodeValueType::Float: result.u32.value = (uint32)number.f32.value; break;
		case (uint32)NodeValueType::Double: result.u32.value = (uint32)number.f64.value; break;
		case (uint32)NodeValueType::Bool: result.u32.value = (uint32)number.b.value; break;
		default: result.u32.value = number.u32.value; break;
		}
	}
	else if constexpr (TV == NodeValueType::Int32)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Uint32: result.i32.value = (int32)number.u32.value; break;
		case (uint32)NodeValueType::Uint64: result.i32.value = (int32)number.u64.value; break;
		case (uint32)NodeValueType::Int64: result.i32.value = (int32)number.i64.value; break;
		case (uint32)NodeValueType::Float: result.i32.value = (int32)number.f32.value; break;
		case (uint32)NodeValueType::Double: result.i32.value = (int32)number.f64.value; break;
		case (uint32)NodeValueType::Bool: result.i32.value = (int32)number.b.value; break;
		default: result.i32.value = number.i32.value; break;
		}
	}
	else if constexpr (TV == NodeValueType::Uint64)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Uint32: result.u64.value = (uint64)number.u32.value; break;
		case (uint32)NodeValueType::Int32: result.u64.value = (uint64)number.i32.value; break;
		case (uint32)NodeValueType::Int64: result.u64.value = (uint64)number.i64.value; break;
		case (uint32)NodeValueType::Float: result.u64.value = (uint64)number.f32.value; break;
		case (uint32)NodeValueType::Double: result.u64.value = (uint64)number.f64.value; break;
		case (uint32)NodeValueType::Bool: result.u64.value = (uint64)number.b.value; break;
		default: result.u64.value = number.u64.value; break;
		}
	}
	else if constexpr (TV == NodeValueType::Int64)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Uint32: result.i64.value = (int64)number.u32.value; break;
		case (uint32)NodeValueType::Int32: result.i64.value = (int64)number.i32.value; break;
		case (uint32)NodeValueType::Uint64: result.i64.value = (int64)number.u64.value; break;
		case (uint32)NodeValueType::Float: result.i64.value = (int64)number.f32.value; break;
		case (uint32)NodeValueType::Double: result.i64.value = (int64)number.f64.value; break;
		case (uint32)NodeValueType::Bool: result.i64.value = (int64)number.b.value; break;
		default: result.i64.value = number.i64.value; break;
		}
	}
	else if constexpr (TV == NodeValueType::Double)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Uint32: result.f64.value = (double)number.u32.value; break;
		case (uint32)NodeValueType::Int32: result.f64.value = (double)number.i32.value; break;
		case (uint32)NodeValueType::Uint64: result.f64.value = (double)number.u64.value; break;
		case (uint32)NodeValueType::Int64: result.f64.value = (double)number.i64.value; break;
		case (uint32)NodeValueType::Float: result.f64.value = (double)number.f32.value; break;
		case (uint32)NodeValueType::Bool: result.f64.value = (double)number.b.value; break;
		default: result.f64.value = number.f64.value; break;
		}
	}
	else if constexpr (TV == NodeValueType::Bool)
	{
		switch (number.base.type)
		{
		case (uint32)NodeValueType::Uint32: result.b.value = number.u32.value != 0; break;
		case (uint32)NodeValueType::Int32: result.b.value = number.i32.value != 0; break;
		case (uint32)NodeValueType::Uint64: result.b.value = number.u64.value != 0; break;
		case (uint32)NodeValueType::Int64: result.b.value = number.i64.value != 0; break;
		case (uint32)NodeValueType::Float: result.b.value = number.f32.value != 0.0f; break;
		case (uint32)NodeValueType::Double: result.b.value = number.f64.value != 0.0; break;
		default: result.b.value = number.b.value; break;
		}
	}
	return result;
}


//**********************************************************************************************************************
template<NodeOperatorType OP>
static NodeValueNumber evaluateEquation(NodeValueNumber left, NodeValueNumber right) noexcept
{
	NodeValueNumber result;
	if (left.base.type == (uint32)NodeValueType::Double || right.base.type == (uint32)NodeValueType::Double)
	{
		left = convertValueNumber<NodeValueType::Double>(left);
		right = convertValueNumber<NodeValueType::Double>(right);
		result.base.type = (uint32)NodeValueType::Double;
		if constexpr (OP == NodeOperatorType::Add)
			result.f64.value = left.f64.value + right.f64.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.f64.value = left.f64.value - right.f64.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.f64.value = left.f64.value * right.f64.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.f64.value = left.f64.value / right.f64.value;
		else abort();
	}
	else if (left.base.type == (uint32)NodeValueType::Float || right.base.type == (uint32)NodeValueType::Float)
	{
		left = convertValueNumber<NodeValueType::Float>(left);
		right = convertValueNumber<NodeValueType::Float>(right);
		result.base.type = (uint32)NodeValueType::Float;
		if constexpr (OP == NodeOperatorType::Add)
			result.f32.value = left.f32.value + right.f32.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.f32.value = left.f32.value - right.f32.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.f32.value = left.f32.value * right.f32.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.f32.value = left.f32.value / right.f32.value;
		else abort();
	}
	else if (left.base.type == (uint32)NodeValueType::Int64 || right.base.type == (uint32)NodeValueType::Int64)
	{
		left = convertValueNumber<NodeValueType::Int64>(left);
		right = convertValueNumber<NodeValueType::Int64>(right);
		result.base.type = (uint32)NodeValueType::Int64;
		if constexpr (OP == NodeOperatorType::Add)
			result.i64.value = left.i64.value + right.i64.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.i64.value = left.i64.value - right.i64.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.i64.value = left.i64.value * right.i64.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.i64.value = left.i64.value / right.i64.value;
		else abort();
	}
	else if (left.base.type == (uint32)NodeValueType::Uint64 || right.base.type == (uint32)NodeValueType::Uint64)
	{
		left = convertValueNumber<NodeValueType::Uint64>(left);
		right = convertValueNumber<NodeValueType::Uint64>(right);
		result.base.type = (uint32)NodeValueType::Uint64;
		if constexpr (OP == NodeOperatorType::Add)
			result.u64.value = left.u64.value + right.u64.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.u64.value = left.u64.value - right.u64.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.u64.value = left.u64.value * right.u64.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.u64.value = left.u64.value / right.u64.value;
		else abort();
	}
	else if (left.base.type == (uint32)NodeValueType::Int32 || right.base.type == (uint32)NodeValueType::Int32)
	{
		left = convertValueNumber<NodeValueType::Int32>(left);
		right = convertValueNumber<NodeValueType::Int32>(right);
		result.base.type = (uint32)NodeValueType::Int32;
		if constexpr (OP == NodeOperatorType::Add)
			result.i32.value = left.i32.value + right.i32.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.i32.value = left.i32.value - right.i32.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.i32.value = left.i32.value * right.i32.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.i32.value = left.i32.value / right.i32.value;
		else abort();
	}
	else if (left.base.type == (uint32)NodeValueType::Uint32 || right.base.type == (uint32)NodeValueType::Uint32)
	{
		left = convertValueNumber<NodeValueType::Uint32>(left);
		right = convertValueNumber<NodeValueType::Uint32>(right);
		result.base.type = (uint32)NodeValueType::Uint32;
		if constexpr (OP == NodeOperatorType::Add)
			result.u32.value = left.u32.value + right.u32.value;
		else if constexpr (OP == NodeOperatorType::Sub)
			result.u32.value = left.u32.value - right.u32.value;
		else if constexpr (OP == NodeOperatorType::Mul)
			result.u32.value = left.u32.value * right.u32.value;
		else if constexpr (OP == NodeOperatorType::Div)
			result.u32.value = left.u32.value / right.u32.value;
		else abort();
	}
	else abort();
	return result;
}

//**********************************************************************************************************************
bool OperatorImGuiNode::evaluate()
{
	auto leftSearch = pinMap->find(leftInPin);
	auto rightSearch = pinMap->find(rightInPin);

	if (leftSearch == pinMap->end() || rightSearch == pinMap->end())
		return false;

	auto leftNode = leftSearch->second;
	auto rightNode = rightSearch->second;

	if (!isNodeValueNumber(leftNode->getValueType()) || isNodeValueNumber(rightNode->getValueType()))
		return false;

	auto leftValue = (NodeValueNumber*)leftNode->getValue();
	auto rightValue = (NodeValueNumber*)rightNode->getValue();

	switch (this->type)
	{
	case NodeOperatorType::Add:
		value = evaluateEquation<NodeOperatorType::Add>(*leftValue, *rightValue);
		break;
	case NodeOperatorType::Sub:
		value = evaluateEquation<NodeOperatorType::Sub>(*leftValue, *rightValue);
		break;
	case NodeOperatorType::Mul:
		value = evaluateEquation<NodeOperatorType::Mul>(*leftValue, *rightValue);
		break;
	case NodeOperatorType::Div:
		value = evaluateEquation<NodeOperatorType::Div>(*leftValue, *rightValue);
		break;
	default: abort();
	}

	return true;
}