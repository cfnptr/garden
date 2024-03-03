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
#include "garden/defines.hpp"
#include "garden/serialize.hpp"

#include "nlohmann/json.hpp"
#include <stack>

namespace garden
{

using json = nlohmann::json;

/*
class JsonDeserializer final : public IDeserializer
{
	fs::path filePath;
	json data;
	stack<json*> hierarchy;

public:
	JsonDeserializer(const fs::path& filePath);
	~JsonDeserializer() final;

	void beginChild(string_view name) final;
	void endChild() final;

	void beginArrayElement() final;
	void endArrayElement() final;

	void read(string_view name, int64 value) final;
	void read(string_view name, uint64 value) final;
	void read(string_view name, int32 value) final;
	void read(string_view name, uint32 value) final;
	void read(string_view name, int16 value) final;
	void read(string_view name, uint16 value) final;
	void read(string_view name, int8 value) final;
	void read(string_view name, uint8 value) final;
	void read(string_view name, bool value) final;
	void read(string_view name, float value) final;
	void read(string_view name, double value) final;
	void read(string_view name, string_view value) final;
	void read(string_view name, int2 value) final;
	void read(string_view name, const int3& value) final;
	void write(string_view name, const int4& value) final;
	void write(string_view name, float2 value) final;
	void write(string_view name, const float3& value) final;
	void write(string_view name, const float4& value) final;
	void write(string_view name, const quat& value) final;
	void write(string_view name, const float2x2& value) final;
	void write(string_view name, const float3x3& value) final;
	void write(string_view name, const float4x4& value) final;
};
*/

} // namespace garden