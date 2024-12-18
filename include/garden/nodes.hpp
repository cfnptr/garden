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
 * @brief Common node functions.
 */

#pragma once
#include "math/types.hpp"

namespace garden
{

using namespace math;

/**
 * @brief Node value types.
 */
enum class NodeValueType : uint32
{
	None, Uint32, Int32, Uint64, Int64, Float, Double, Bool, NumberPair, Count
};
/**
 * @brief Node operator types.
 */
enum class NodeOperatorType : uint32
{
	Add, Sub, Mul, Div, Count
};

struct NodeValue final
{
	uint32 type = (uint32)NodeValueType::Int32;
};
struct NodeValueInt32 final
{
	uint32 type = (uint32)NodeValueType::Int32;
	int32 value = 0;
};
struct NodeValueUint32 final
{
	uint32 type = (uint32)NodeValueType::Uint32;
	uint32 value = 0;
};
struct NodeValueInt64 final
{
	uint32 type = (uint32)NodeValueType::Int64;
	int64 value = 0;
};
struct NodeValueUint64 final
{
	uint32 type = (uint32)NodeValueType::Uint64;
	uint64 value = 0;
};
struct NodeValueFloat final
{
	uint32 type = (uint32)NodeValueType::Float;
	float value = 0.0f;
};
struct NodeValueDouble final
{
	uint32 type = (uint32)NodeValueType::Double;
	double value = 0.0;
};
struct NodeValueBool final
{
	uint32 type = (uint32)NodeValueType::Bool;
	bool value = false;
};

/**
 * @brief Node number value container.
 */
union NodeValueNumber
{
	NodeValue base;
	NodeValueInt32 i32;
	NodeValueUint32 u32;
	NodeValueInt64 i64;
	NodeValueUint64 u64;
	NodeValueFloat f32;
	NodeValueDouble f64;
	NodeValueBool b;
	NodeValueNumber() : u64() {}
};
/**
 * @brief Node number value pair container.
 */
struct NodeNumberPair final
{
	uint32 type = (uint32)NodeValueType::NumberPair;
	NodeValueNumber left;
	NodeValueNumber right;
};

/***********************************************************************************************************************
 * @brief Is the node value a number.
 * @param valueType target node value type
 */
static constexpr bool isNodeValueNumber(uint32 valueType) noexcept
{
	return (uint32)NodeValueType::Uint32 <= valueType && valueType <= (uint32)NodeValueType::Bool;
}

} // namespace garden