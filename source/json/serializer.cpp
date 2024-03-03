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

#include "garden/json/serializer.hpp"
#include <fstream>

using namespace garden;

//**********************************************************************************************************************
JsonSerializer::JsonSerializer(const fs::path& filePath)
{
	this->filePath = filePath;
	hierarchy.emplace(&data);
}
JsonSerializer::~JsonSerializer()
{
	std::ofstream fileStream(filePath);
	fileStream << std::setw(4) << data;
}

//**********************************************************************************************************************
void JsonSerializer::beginChild(string_view name)
{
	hierarchy.emplace(&data[name]);
}
void JsonSerializer::endChild()
{
	GARDEN_ASSERT(hierarchy.size() > 1); // No child to end.
	hierarchy.pop();
}

void JsonSerializer::beginArrayElement()
{
	auto& object = *hierarchy.top();
	auto element = &object.emplace_back(json());
	hierarchy.emplace(element);
}
void JsonSerializer::endArrayElement()
{
	GARDEN_ASSERT(hierarchy.size() > 1); // No element to end.
	hierarchy.pop();
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, int64 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint64 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int32 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint32 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int16 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint16 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int8 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint8 value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, bool value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, float value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, double value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, string_view value)
{
	auto& object = *hierarchy.top();
	object[name] = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, int2 value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y };
}
void JsonSerializer::write(string_view name, const int3& value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y, value.z };
}
void JsonSerializer::write(string_view name, const int4& value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y, value.z, value.w };
}
void JsonSerializer::write(string_view name, float2 value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y };
}
void JsonSerializer::write(string_view name, const float3& value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y, value.z };
}
void JsonSerializer::write(string_view name, const float4& value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y, value.z, value.w };
}
void JsonSerializer::write(string_view name, const quat& value)
{
	auto& object = *hierarchy.top();
	object[name] = { value.x, value.y, value.z, value.w };
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, const float2x2& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		value.c0.x, value.c0.y,
		value.c1.x, value.c1.y
	};
}
void JsonSerializer::write(string_view name, const float3x3& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		value.c0.x, value.c0.y, value.c0.z,
		value.c1.x, value.c1.y, value.c1.z,
		value.c2.x, value.c2.y, value.c2.z
	};
}
void JsonSerializer::write(string_view name, const float4x4& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		value.c0.x, value.c0.y, value.c0.z, value.c0.w,
		value.c1.x, value.c1.y, value.c1.z, value.c1.w,
		value.c2.x, value.c2.y, value.c2.z, value.c2.w,
		value.c3.x, value.c3.y, value.c3.z, value.c3.w
	};
}