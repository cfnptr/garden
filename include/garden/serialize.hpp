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
 */

#pragma once
#include "ecsm.hpp"
#include "math/matrix.hpp"

namespace garden
{

using namespace std;
using namespace math;
using namespace ecsm;

class ISerializer
{
public:
	virtual ~ISerializer() = 0;

	virtual void beginChild(const string& name) = 0;
	virtual void endChild() = 0;

	virtual void beginArray(const string& name) = 0;
	virtual void endArray() = 0;

	virtual void write(const string& name, int64 value) = 0;
	virtual void write(const string& name, uint64 value) = 0;
	virtual void write(const string& name, int32 value) = 0;
	virtual void write(const string& name, uint32 value) = 0;
	virtual void write(const string& name, int16 value) = 0;
	virtual void write(const string& name, uint16 value) = 0;
	virtual void write(const string& name, int8 value) = 0;
	virtual void write(const string& name, uint8 value) = 0;
	virtual void write(const string& name, bool value) = 0;
	virtual void write(const string& name, float value, uint8 precision = 0) = 0;
	virtual void write(const string& name, double value, uint8 precision = 0) = 0;
	virtual void write(const string& name, string_view value) = 0;
	virtual void write(const string& name, int2 value) = 0;
	virtual void write(const string& name, const int3& value) = 0;
	virtual void write(const string& name, const int4& value) = 0;
	virtual void write(const string& name, float2 value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const float3& value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const float4& value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const quat& value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const float2x2& value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const float3x3& value, uint8 precision = 0) = 0;
	virtual void write(const string& name, const float4x4& value, uint8 precision = 0) = 0;
};
class IDeserializer
{
public:
	virtual ~IDeserializer() = 0;

	virtual bool beginChild(const string& name) = 0;
	virtual void endChild() = 0;

	virtual bool beginArray(const string& name) = 0;
	virtual void endArray() = 0;

	virtual void read(const string& name, int64& value) = 0;
	virtual void read(const string& name, uint64& value) = 0;
	virtual void read(const string& name, int32& value) = 0;
	virtual void read(const string& name, uint32& value) = 0;
	virtual void read(const string& name, int16& value) = 0;
	virtual void read(const string& name, uint16& value) = 0;
	virtual void read(const string& name, int8& value) = 0;
	virtual void read(const string& name, uint8& value) = 0;
	virtual void read(const string& name, bool& value) = 0;
	virtual void read(const string& name, float& value) = 0;
	virtual void read(const string& name, double& value) = 0;
	virtual void read(const string& name, string& value) = 0;
	virtual void read(const string& name, int2& value) = 0;
	virtual void read(const string& name, int3& value) = 0;
	virtual void read(const string& name, int4& value) = 0;
	virtual void read(const string& name, float2& value) = 0;
	virtual void read(const string& name, float3& value) = 0;
	virtual void read(const string& name, float4& value) = 0;
	virtual void read(const string& name, quat& value) = 0;
	virtual void read(const string& name, float2x2& value) = 0;
	virtual void read(const string& name, float3x3& value) = 0;
	virtual void read(const string& name, float4x4& value) = 0;
};

class ISerializable
{
protected:
	virtual void serialize(ISerializer& serializer, ID<Entity> entity) = 0;
	virtual void deserialize(IDeserializer& deserializer, ID<Entity> entity) = 0;
};

} // namespace garden
