// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
	stack<json*, vector<json*>> hierarchy;
public:
	JsonSerializer();
	JsonSerializer(const fs::path& filePath);
	~JsonSerializer() override;

	void setFilePath(const fs::path& filePath);

	JsonSerializer(const JsonSerializer&) = delete;
	JsonSerializer(JsonSerializer&&) = delete;
	JsonSerializer& operator=(const JsonSerializer&) = delete;
	JsonSerializer& operator=(JsonSerializer&&) = delete;

	void beginChild(string_view name) override;
	void endChild() override;

	void beginArrayElement() override;
	void write(int64 value) override;
	void write(uint64 value) override;
	void write(int32 value) override;
	void write(uint32 value) override;
	void write(int16 value) override;
	void write(uint16 value) override;
	void write(int8 value) override;
	void write(uint8 value) override;
	void write(bool value) override;
	void write(float value) override;
	void write(double value) override;
	void write(string_view value) override;
	void endArrayElement() override;

	void write(string_view name, int64 value) override;
	void write(string_view name, uint64 value) override;
	void write(string_view name, int32 value) override;
	void write(string_view name, uint32 value) override;
	void write(string_view name, int16 value) override;
	void write(string_view name, uint16 value) override;
	void write(string_view name, int8 value) override;
	void write(string_view name, uint8 value) override;
	void write(string_view name, bool value) override;
	void write(string_view name, float value) override;
	void write(string_view name, double value) override;
	void write(string_view name, string_view value) override;
	void write(string_view name, u32string_view value) override;
	void write(string_view name, int2 value) override;
	void write(string_view name, int3 value) override;
	void write(string_view name, int4 value) override;
	void write(string_view name, uint2 value) override;
	void write(string_view name, uint3 value) override;
	void write(string_view name, uint4 value) override;
	void write(string_view name, float2 value) override;
	void write(string_view name, float3 value) override;
	void write(string_view name, float4 value) override;
	void write(string_view name, quat value) override;
	void write(string_view name, const float2x2& value) override;
	void write(string_view name, const float3x3& value) override;
	void write(string_view name, const float4x4& value) override;
	void write(string_view name, const Aabb& value) override;
	void write(string_view name, Color value, bool rgb = false) override;

	string toString() const;
};

class JsonDeserializer final : public IDeserializer
{
	json data;
	stack<json*, vector<json*>> hierarchy;
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

	bool beginChild(string_view name) override;
	void endChild() override;

	psize getArraySize() override;
	bool beginArrayElement(psize index) override;
	bool read(int64& value) override;
	bool read(uint64& value) override;
	bool read(int32& value) override;
	bool read(uint32& value) override;
	bool read(int16& value) override;
	bool read(uint16& value) override;
	bool read(int8& value) override;
	bool read(uint8& value) override;
	bool read(bool& value) override;
	bool read(float& value) override;
	bool read(double& value) override;
	bool read(string& value) override;
	void endArrayElement() override;

	bool read(string_view name, int64& value) override;
	bool read(string_view name, uint64& value) override;
	bool read(string_view name, int32& value) override;
	bool read(string_view name, uint32& value) override;
	bool read(string_view name, int16& value) override;
	bool read(string_view name, uint16& value) override;
	bool read(string_view name, int8& value) override;
	bool read(string_view name, uint8& value) override;
	bool read(string_view name, bool& value) override;
	bool read(string_view name, volatile bool& value) override;
	bool read(string_view name, float& value) override;
	bool read(string_view name, double& value) override;
	bool read(string_view name, string& value) override;
	bool read(string_view name, u32string& value) override;
	bool read(string_view name, int2& value) override;
	bool read(string_view name, int3& value) override;
	bool read(string_view name, int4& value) override;
	bool read(string_view name, uint2& value) override;
	bool read(string_view name, uint3& value) override;
	bool read(string_view name, uint4& value) override;
	bool read(string_view name, float2& value) override;
	bool read(string_view name, float3& value) override;
	bool read(string_view name, float4& value) override;
	bool read(string_view name, quat& value) override;
	bool read(string_view name, float2x2& value) override;
	bool read(string_view name, float3x3& value) override;
	bool read(string_view name, float4x4& value) override;
	bool read(string_view name, Aabb& value) override;
	bool read(string_view name, Color& value) override;
	bool read(string_view name, f32x4& value, uint8 components = 4) override;
};

} // namespace garden