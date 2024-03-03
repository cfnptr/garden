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

/**********************************************************************************************************************
 * @file
 * @brief Data serialization functions.
 * 
 * @details
 * Data serialization is the process of converting structured data, such as objects or data structures, 
 * into a format that can be easily stored, transmitted, or reconstructed later. This serialized data 
 * can then be saved to a file, sent over a network, or otherwise persisted.
 */

#pragma once
#include "ecsm.hpp"
#include "math/matrix.hpp"

namespace garden
{

using namespace std;
using namespace math;
using namespace ecsm;

/**
 * @brief Base serializer interface.
 */
class ISerializer
{
public:
	virtual ~ISerializer() { }

	virtual void beginChild(string_view name) = 0;
	virtual void endChild() = 0;

	virtual void beginArrayElement() = 0;
	virtual void endArrayElement() = 0;

	virtual void write(string_view name, int64 value) = 0;
	virtual void write(string_view name, uint64 value) = 0;
	virtual void write(string_view name, int32 value) = 0;
	virtual void write(string_view name, uint32 value) = 0;
	virtual void write(string_view name, int16 value) = 0;
	virtual void write(string_view name, uint16 value) = 0;
	virtual void write(string_view name, int8 value) = 0;
	virtual void write(string_view name, uint8 value) = 0;
	virtual void write(string_view name, bool value) = 0;
	virtual void write(string_view name, float value) = 0;
	virtual void write(string_view name, double value) = 0;
	virtual void write(string_view name, string_view value) = 0;
	virtual void write(string_view name, int2 value) = 0;
	virtual void write(string_view name, const int3& value) = 0;
	virtual void write(string_view name, const int4& value) = 0;
	virtual void write(string_view name, float2 value) = 0;
	virtual void write(string_view name, const float3& value) = 0;
	virtual void write(string_view name, const float4& value) = 0;
	virtual void write(string_view name, const quat& value) = 0;
	virtual void write(string_view name, const float2x2& value) = 0;
	virtual void write(string_view name, const float3x3& value) = 0;
	virtual void write(string_view name, const float4x4& value) = 0;

	// TODO: write array of values.
};

/**
 * @brief Base deserializer interface.
 */
class IDeserializer
{
public:
	virtual ~IDeserializer() { }

	virtual bool beginChild(string_view name) = 0;
	virtual void endChild() = 0;

	virtual bool beginArray(string_view name) = 0;
	virtual void endArray() = 0;

	virtual void read(string_view name, int64& value) = 0;
	virtual void read(string_view name, uint64& value) = 0;
	virtual void read(string_view name, int32& value) = 0;
	virtual void read(string_view name, uint32& value) = 0;
	virtual void read(string_view name, int16& value) = 0;
	virtual void read(string_view name, uint16& value) = 0;
	virtual void read(string_view name, int8& value) = 0;
	virtual void read(string_view name, uint8& value) = 0;
	virtual void read(string_view name, bool& value) = 0;
	virtual void read(string_view name, float& value) = 0;
	virtual void read(string_view name, double& value) = 0;
	virtual void read(string_view name, string& value) = 0;
	virtual void read(string_view name, int2& value) = 0;
	virtual void read(string_view name, int3& value) = 0;
	virtual void read(string_view name, int4& value) = 0;
	virtual void read(string_view name, float2& value) = 0;
	virtual void read(string_view name, float3& value) = 0;
	virtual void read(string_view name, float4& value) = 0;
	virtual void read(string_view name, quat& value) = 0;
	virtual void read(string_view name, float2x2& value) = 0;
	virtual void read(string_view name, float3x3& value) = 0;
	virtual void read(string_view name, float4x4& value) = 0;

	// TODO: read array of values.
};

/**
 * @brief Base serializable interface.
 */
class ISerializable
{
public:
	virtual void serialize(ISerializer& serializer, ID<Component> component) = 0;
	virtual void deserialize(IDeserializer& deserializer, ID<Component> component) = 0;
};

} // namespace garden