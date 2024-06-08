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

#include "garden/json-serialize.hpp"
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
	fileStream << std::setw(1) << std::setfill('\t')  << data;
}

void JsonSerializer::beginChild(string_view name)
{
	auto& object = *hierarchy.top();
	hierarchy.emplace(&object[name]);
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
	object[name] = { { "x", value.x }, { "y", value.y } };
}
void JsonSerializer::write(string_view name, const int3& value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
}
void JsonSerializer::write(string_view name, const int4& value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}
void JsonSerializer::write(string_view name, float2 value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y } };
}
void JsonSerializer::write(string_view name, const float3& value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
}
void JsonSerializer::write(string_view name, const float4& value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}
void JsonSerializer::write(string_view name, const quat& value)
{
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, const float2x2& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		{ "00", value.c0.x }, { "01", value.c0.y },
		{ "10", value.c1.x }, { "11", value.c1.y }
	};
}
void JsonSerializer::write(string_view name, const float3x3& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		{ "00", value.c0.x }, { "01", value.c0.y }, { "02", value.c0.z },
		{ "10", value.c1.x }, { "11", value.c1.y }, { "12", value.c1.z },
		{ "20", value.c2.x }, { "21", value.c2.y }, { "22", value.c2.z }
	};
}
void JsonSerializer::write(string_view name, const float4x4& value)
{
	auto& object = *hierarchy.top();
	object[name] =
	{
		{ "00", value.c0.x }, { "01", value.c0.y }, { "02", value.c0.z }, { "03", value.c0.w },
		{ "10", value.c1.x }, { "11", value.c1.y }, { "12", value.c1.z }, { "13", value.c1.w },
		{ "20", value.c2.x }, { "21", value.c2.y }, { "22", value.c2.z }, { "23", value.c2.w },
		{ "30", value.c3.x }, { "31", value.c3.y }, { "32", value.c3.z }, { "33", value.c3.w }
	};
}
void JsonSerializer::write(string_view name, const Aabb& value)
{
	beginChild(name);
	write("min", value.getMin());
	write("max", value.getMax());
	endChild();
}

//**********************************************************************************************************************
JsonDeserializer::JsonDeserializer(string_view json)
{
	data = json::parse(json);
	hierarchy.emplace(&data);
}
JsonDeserializer::JsonDeserializer(const vector<uint8>& bson)
{
	data = json::parse(bson);
	hierarchy.emplace(&data);
}
JsonDeserializer::JsonDeserializer(const fs::path& filePath)
{
	std::ifstream fileStream(filePath);
	fileStream >> data;
	hierarchy.emplace(&data);
}

//**********************************************************************************************************************
bool JsonDeserializer::beginChild(string_view name)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_structured())
		return false;
	hierarchy.emplace(&i);
	return true;
}
void JsonDeserializer::endChild()
{
	GARDEN_ASSERT(hierarchy.size() > 1); // No child to end.
	hierarchy.pop();
}

psize JsonDeserializer::getArraySize()
{
	auto& object = *hierarchy.top();
	return object.size();
}
bool JsonDeserializer::beginArrayElement(psize index)
{
	auto& object = *hierarchy.top();
	if (!object.is_array())
		return false;
	auto& i = object.at(index);
	hierarchy.emplace(&i);
	return true;
}
void JsonDeserializer::endArrayElement()
{
	GARDEN_ASSERT(hierarchy.size() > 1); // No element to end.
	hierarchy.pop();
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, int64& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (int64)i;
}
void JsonDeserializer::read(string_view name, uint64& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (uint64)i;
}
void JsonDeserializer::read(string_view name, int32& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (int32)i;
}
void JsonDeserializer::read(string_view name, uint32& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (uint32)i;
}
void JsonDeserializer::read(string_view name, int16& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (int16)i;
}
void JsonDeserializer::read(string_view name, uint16& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (uint16)i;
}
void JsonDeserializer::read(string_view name, int8& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (int8)i;
}
void JsonDeserializer::read(string_view name, uint8& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return;
	value = (uint8)i;
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, bool& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_boolean())
		return;
	value = (bool)i;
}
void JsonDeserializer::read(string_view name, float& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_float())
		return;
	value = (float)i;
}
void JsonDeserializer::read(string_view name, double& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_float())
		return;
	value = (double)i;
}
void JsonDeserializer::read(string_view name, string& value)
{
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_string())
		return;
	value = i;
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, int2& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_integer())
		value.x = (int32)*i;
	i = &a["y"];
	if (i->is_number_integer())
		value.y = (int32)*i;
}
void JsonDeserializer::read(string_view name, int3& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_integer())
		value.x = (int32)*i;
	i = &a["y"];
	if (i->is_number_integer())
		value.y = (int32)*i;
	i = &a["z"];
	if (i->is_number_integer())
		value.z = (int32)*i;
}
void JsonDeserializer::read(string_view name, int4& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_integer())
		value.x = (int32)*i;
	i = &a["y"];
	if (i->is_number_integer())
		value.y = (int32)*i;
	i = &a["z"];
	if (i->is_number_integer())
		value.z = (int32)*i;
	i = &a["w"];
	if (i->is_number_integer())
		value.w = (int32)*i;
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, float2& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_float())
		value.x = (float)*i;
	i = &a["y"];
	if (i->is_number_float())
		value.y = (float)*i;
}
void JsonDeserializer::read(string_view name, float3& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_float())
		value.x = (float)*i;
	i = &a["y"];
	if (i->is_number_float())
		value.y = (float)*i;
	i = &a["z"];
	if (i->is_number_float())
		value.z = (float)*i;
}
void JsonDeserializer::read(string_view name, float4& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_float())
		value.x = (float)*i;
	i = &a["y"];
	if (i->is_number_float())
		value.y = (float)*i;
	i = &a["z"];
	if (i->is_number_float())
		value.z = (float)*i;
	i = &a["w"];
	if (i->is_number_float())
		value.w = (float)*i;
}
void JsonDeserializer::read(string_view name, quat& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["x"];
	if (i->is_number_float())
		value.x = (float)*i;
	i = &a["y"];
	if (i->is_number_float())
		value.y = (float)*i;
	i = &a["z"];
	if (i->is_number_float())
		value.z = (float)*i;
	i = &a["w"];
	if (i->is_number_float())
		value.w = (float)*i;
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, float2x2& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i;
}
void JsonDeserializer::read(string_view name, float3x3& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i;
	i = &a["02"]; if (i->is_number_float()) value.c0.z = (float)*i;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i;
	i = &a["12"]; if (i->is_number_float()) value.c1.z = (float)*i;

	i = &a["20"]; if (i->is_number_float()) value.c2.x = (float)*i;
	i = &a["21"]; if (i->is_number_float()) value.c2.y = (float)*i;
	i = &a["22"]; if (i->is_number_float()) value.c2.z = (float)*i;
}
void JsonDeserializer::read(string_view name, float4x4& value)
{
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return;

	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i;
	i = &a["02"]; if (i->is_number_float()) value.c0.z = (float)*i;
	i = &a["03"]; if (i->is_number_float()) value.c0.w = (float)*i;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i;
	i = &a["12"]; if (i->is_number_float()) value.c1.z = (float)*i;
	i = &a["13"]; if (i->is_number_float()) value.c1.w = (float)*i;

	i = &a["20"]; if (i->is_number_float()) value.c2.x = (float)*i;
	i = &a["21"]; if (i->is_number_float()) value.c2.y = (float)*i;
	i = &a["22"]; if (i->is_number_float()) value.c2.z = (float)*i;
	i = &a["23"]; if (i->is_number_float()) value.c2.w = (float)*i;

	i = &a["30"]; if (i->is_number_float()) value.c3.x = (float)*i;
	i = &a["31"]; if (i->is_number_float()) value.c3.y = (float)*i;
	i = &a["32"]; if (i->is_number_float()) value.c3.z = (float)*i;
	i = &a["33"]; if (i->is_number_float()) value.c3.w = (float)*i;
}

//**********************************************************************************************************************
void JsonDeserializer::read(string_view name, Aabb& value)
{
	if (beginChild(name))
	{
		auto min = value.getMin();
		read("min", min);
		auto max = value.getMin();
		read("max", max);
		if (min <= max)
			value.set(min, max);
		endChild();
	}
}