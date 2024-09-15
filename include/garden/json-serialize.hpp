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

#pragma once
#include "garden/serialize.hpp"
#include "nlohmann/json.hpp"
#include <stack>

namespace garden
{

using json = nlohmann::json;

class JsonSerializer final : public ISerializer
{
	fs::path filePath;
	json data;
	stack<json*> hierarchy;
public:
	JsonSerializer();
	JsonSerializer(const fs::path& filePath);
	~JsonSerializer() final;

	void setFilePath(const fs::path& filePath);

	JsonSerializer(const JsonSerializer&) = delete;
	JsonSerializer(JsonSerializer&&) = delete;
	JsonSerializer& operator=(const JsonSerializer&) = delete;
	JsonSerializer& operator=(JsonSerializer&&) = delete;

	void beginChild(string_view name) final;
	void endChild() final;

	void beginArrayElement() final;
	void write(int64 value) final;
	void write(uint64 value) final;
	void write(int32 value) final;
	void write(uint32 value) final;
	void write(int16 value) final;
	void write(uint16 value) final;
	void write(int8 value) final;
	void write(uint8 value) final;
	void write(bool value) final;
	void write(float value) final;
	void write(double value) final;
	void write(string_view value) final;
	void endArrayElement() final;

	void write(string_view name, int64 value) final;
	void write(string_view name, uint64 value) final;
	void write(string_view name, int32 value) final;
	void write(string_view name, uint32 value) final;
	void write(string_view name, int16 value) final;
	void write(string_view name, uint16 value) final;
	void write(string_view name, int8 value) final;
	void write(string_view name, uint8 value) final;
	void write(string_view name, bool value) final;
	void write(string_view name, float value) final;
	void write(string_view name, double value) final;
	void write(string_view name, string_view value) final;
	void write(string_view name, int2 value) final;
	void write(string_view name, const int3& value) final;
	void write(string_view name, const int4& value) final;
	void write(string_view name, uint2 value) final;
	void write(string_view name, const uint3& value) final;
	void write(string_view name, const uint4& value) final;
	void write(string_view name, float2 value) final;
	void write(string_view name, const float3& value) final;
	void write(string_view name, const float4& value) final;
	void write(string_view name, const quat& value) final;
	void write(string_view name, const float2x2& value) final;
	void write(string_view name, const float3x3& value) final;
	void write(string_view name, const float4x4& value) final;
	void write(string_view name, const Aabb& value) final;

	string toString() const;
};

class JsonDeserializer final : public IDeserializer
{
	json data;
	stack<json*> hierarchy;
public:
	JsonDeserializer();
	JsonDeserializer(string_view json) { load(json); }
	JsonDeserializer(const vector<uint8>& bson) { load(bson); }
	JsonDeserializer(const fs::path& filePath) { load(filePath); }

	JsonDeserializer(const JsonDeserializer&) = delete;
	JsonDeserializer(JsonDeserializer&&) = delete;
	JsonDeserializer& operator=(const JsonDeserializer&) = delete;
	JsonDeserializer& operator=(JsonDeserializer&&) = delete;

	void load(string_view json);
	void load(const vector<uint8>& bson);
	void load(const fs::path& filePath);

	bool beginChild(string_view name) final;
	void endChild() final;

	psize getArraySize() final;
	bool beginArrayElement(psize index) final;
	bool read(int64& value) final;
	bool read(uint64& value) final;
	bool read(int32& value) final;
	bool read(uint32& value) final;
	bool read(int16& value) final;
	bool read(uint16& value) final;
	bool read(int8& value) final;
	bool read(uint8& value) final;
	bool read(bool& value) final;
	bool read(float& value) final;
	bool read(double& value) final;
	bool read(string& value) final;
	void endArrayElement() final;

	bool read(string_view name, int64& value) final;
	bool read(string_view name, uint64& value) final;
	bool read(string_view name, int32& value) final;
	bool read(string_view name, uint32& value) final;
	bool read(string_view name, int16& value) final;
	bool read(string_view name, uint16& value) final;
	bool read(string_view name, int8& value) final;
	bool read(string_view name, uint8& value) final;
	bool read(string_view name, bool& value) final;
	bool read(string_view name, float& value) final;
	bool read(string_view name, double& value) final;
	bool read(string_view name, string& value) final;
	bool read(string_view name, int2& value) final;
	bool read(string_view name, int3& value) final;
	bool read(string_view name, int4& value) final;
	bool read(string_view name, uint2& value) final;
	bool read(string_view name, uint3& value) final;
	bool read(string_view name, uint4& value) final;
	bool read(string_view name, float2& value) final;
	bool read(string_view name, float3& value) final;
	bool read(string_view name, float4& value) final;
	bool read(string_view name, quat& value) final;
	bool read(string_view name, float2x2& value) final;
	bool read(string_view name, float3x3& value) final;
	bool read(string_view name, float4x4& value) final;
	bool read(string_view name, Aabb& value) final;
};

} // namespace garden