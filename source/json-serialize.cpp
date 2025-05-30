// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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
#include <sstream>

using namespace garden;

//**********************************************************************************************************************
JsonSerializer::JsonSerializer()
{
	hierarchy.emplace(&data);
}
JsonSerializer::JsonSerializer(const fs::path& filePath)
{
	hierarchy.emplace(&data);
	setFilePath(filePath);
}
JsonSerializer::~JsonSerializer()
{
	if (!filePath.empty())
	{
		std::ofstream fileStream(filePath);
		fileStream << std::setw(1) << std::setfill('\t') << data;
	}
}

void JsonSerializer::setFilePath(const fs::path& filePath)
{
	this->filePath = filePath;
}

void JsonSerializer::beginChild(string_view name)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	hierarchy.emplace(&object[name]);
}
void JsonSerializer::endChild()
{
	GARDEN_ASSERT_MSG(hierarchy.size() > 1, "No child to end");
	hierarchy.pop();
}

//**********************************************************************************************************************
void JsonSerializer::beginArrayElement()
{
	auto& object = *hierarchy.top();
	auto element = &object.emplace_back(json());
	hierarchy.emplace(element);
}

void JsonSerializer::write(int64 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(uint64 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(int32 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(uint32 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(int16 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(uint16 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(int8 value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(uint8 value)
{
	auto& object = *hierarchy.top();
	object = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(bool value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(float value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(double value)
{
	auto& object = *hierarchy.top();
	object = value;
}
void JsonSerializer::write(string_view value)
{
	auto& object = *hierarchy.top();
	object = value;
}

void JsonSerializer::endArrayElement()
{
	GARDEN_ASSERT_MSG(hierarchy.size() > 1, "No array element to end");
	hierarchy.pop();
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, int64 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint64 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int32 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint32 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int16 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint16 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, int8 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, uint8 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, bool value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, float value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, double value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}
void JsonSerializer::write(string_view name, string_view value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, int2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y } };
}
void JsonSerializer::write(string_view name, int3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
}
void JsonSerializer::write(string_view name, int4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}
void JsonSerializer::write(string_view name, uint2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y } };
}
void JsonSerializer::write(string_view name, uint3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
}
void JsonSerializer::write(string_view name, uint4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}
void JsonSerializer::write(string_view name, float2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y } };
}
void JsonSerializer::write(string_view name, float3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
}
void JsonSerializer::write(string_view name, float4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
}
void JsonSerializer::write(string_view name, quat value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] = { { "x", value.getX() }, { "y", value.getY() }, { "z", value.getZ() }, { "w", value.getW() } };
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, const float2x2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	object[name] =
	{
		{ "00", value.c0.x }, { "01", value.c0.y },
		{ "10", value.c1.x }, { "11", value.c1.y }
	};
}
void JsonSerializer::write(string_view name, const float3x3& value)
{
	GARDEN_ASSERT(!name.empty());
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
	GARDEN_ASSERT(!name.empty());
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
	GARDEN_ASSERT(!name.empty());
	beginChild(name);
	write("min", (float3)value.getMin());
	write("max", (float3)value.getMax());
	endChild();
}

string JsonSerializer::toString() const
{
	stringstream stringStream;
	stringStream << std::setw(1) << std::setfill('\t') << data;
	return stringStream.str();
}

//**********************************************************************************************************************
JsonDeserializer::JsonDeserializer()
{
	hierarchy.emplace(&data);
}

void JsonDeserializer::load(string_view json)
{
	GARDEN_ASSERT(!json.empty());
	data = json::parse(json);
	hierarchy = {};
	hierarchy.emplace(&data);
}
void JsonDeserializer::load(const vector<uint8>& bson)
{
	GARDEN_ASSERT(!bson.empty());
	data = json::from_bson(bson);
	hierarchy = {};
	hierarchy.emplace(&data);
}
void JsonDeserializer::load(const fs::path& filePath)
{
	GARDEN_ASSERT(!filePath.empty());
	std::ifstream fileStream(filePath);
	fileStream >> data;
	hierarchy = {};
	hierarchy.emplace(&data);
}

bool JsonDeserializer::beginChild(string_view name)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_structured())
		return false;
	hierarchy.emplace(&i);
	return true;
}
void JsonDeserializer::endChild()
{
	GARDEN_ASSERT_MSG(hierarchy.size() > 1, "No child to end");
	hierarchy.pop();
}

//**********************************************************************************************************************
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

bool JsonDeserializer::read(int64& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (int64)object;
	return true;
}
bool JsonDeserializer::read(uint64& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (uint64)object;
	return true;
}
bool JsonDeserializer::read(int32& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (int32)object;
	return true;
}
bool JsonDeserializer::read(uint32& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (uint32)object;
	return true;
}
bool JsonDeserializer::read(int16& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (int16)object;
	return true;
}
bool JsonDeserializer::read(uint16& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (uint16)object;
	return true;
}
bool JsonDeserializer::read(int8& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (int8)object;
	return true;
}
bool JsonDeserializer::read(uint8& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_integer())
		return false;
	value = (uint8)object;
	return true;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(bool& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_boolean())
		return false;
	value = (bool)object;
	return true;
}
bool JsonDeserializer::read(float& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_float() && !object.is_number_integer())
		return false;
	value = (float)object;
	return true;
}
bool JsonDeserializer::read(double& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_number_float() && !object.is_number_integer())
		return false;
	value = (double)object;
	return true;
}
bool JsonDeserializer::read(string& value)
{
	auto& object = *hierarchy.top();
	if (!object.is_string())
		return false;
	value = (string)object;
	return true;
}

void JsonDeserializer::endArrayElement()
{
	GARDEN_ASSERT_MSG(hierarchy.size() > 1, "No array element to end");
	hierarchy.pop();
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, int64& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (int64)i;
	return true;
}
bool JsonDeserializer::read(string_view name, uint64& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (uint64)i;
	return true;
}
bool JsonDeserializer::read(string_view name, int32& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (int32)i;
	return true;
}
bool JsonDeserializer::read(string_view name, uint32& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (uint32)i;
	return true;
}
bool JsonDeserializer::read(string_view name, int16& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (int16)i;
	return true;
}
bool JsonDeserializer::read(string_view name, uint16& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (uint16)i;
	return true;
}
bool JsonDeserializer::read(string_view name, int8& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (int8)i;
	return true;
}
bool JsonDeserializer::read(string_view name, uint8& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_integer())
		return false;
	value = (uint8)i;
	return true;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, bool& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_boolean())
		return false;
	value = (bool)i;
	return true;
}
bool JsonDeserializer::read(string_view name, float& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_float())
		return false;
	value = (float)i;
	return true;
}
bool JsonDeserializer::read(string_view name, double& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_number_float())
		return false;
	value = (double)i;
	return true;
}
bool JsonDeserializer::read(string_view name, string& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& i = object[name];
	if (!i.is_string())
		return false;
	value.assign(i);
	return true;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, int2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &a["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = true;
	return result;
}
bool JsonDeserializer::read(string_view name, int3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &a["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = false;
	i = &a["z"]; if (i->is_number_integer()) value.z = (int32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, int4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &a["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = false;
	i = &a["z"]; if (i->is_number_integer()) value.z = (int32)*i; else result = false;
	i = &a["w"]; if (i->is_number_integer()) value.w = (int32)*i; else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, uint2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &a["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = true;
	return result;
}
bool JsonDeserializer::read(string_view name, uint3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &a["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = false;
	i = &a["z"]; if (i->is_number_unsigned()) value.z = (uint32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, uint4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &a["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = false;
	i = &a["z"]; if (i->is_number_unsigned()) value.z = (uint32)*i; else result = false;
	i = &a["w"]; if (i->is_number_unsigned()) value.w = (uint32)*i; else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, float2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &a["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &a["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	i = &a["z"]; if (i->is_number_float()) value.z = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &a["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	i = &a["z"]; if (i->is_number_float()) value.z = (float)*i; else result = false;
	i = &a["w"]; if (i->is_number_float()) value.w = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, quat& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["x"]; if (i->is_number_float()) value.setX((float)*i); else result = false;
	i = &a["y"]; if (i->is_number_float()) value.setY((float)*i); else result = false;
	i = &a["z"]; if (i->is_number_float()) value.setZ((float)*i); else result = false;
	i = &a["w"]; if (i->is_number_float()) value.setW((float)*i); else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, float2x2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float3x3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;
	i = &a["02"]; if (i->is_number_float()) value.c0.z = (float)*i; else result = false;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	i = &a["12"]; if (i->is_number_float()) value.c1.z = (float)*i; else result = false;

	i = &a["20"]; if (i->is_number_float()) value.c2.x = (float)*i; else result = false;
	i = &a["21"]; if (i->is_number_float()) value.c2.y = (float)*i; else result = false;
	i = &a["22"]; if (i->is_number_float()) value.c2.z = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float4x4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	auto i = &a["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &a["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;
	i = &a["02"]; if (i->is_number_float()) value.c0.z = (float)*i; else result = false;
	i = &a["03"]; if (i->is_number_float()) value.c0.w = (float)*i; else result = false;

	i = &a["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &a["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	i = &a["12"]; if (i->is_number_float()) value.c1.z = (float)*i; else result = false;
	i = &a["13"]; if (i->is_number_float()) value.c1.w = (float)*i; else result = false;

	i = &a["20"]; if (i->is_number_float()) value.c2.x = (float)*i; else result = false;
	i = &a["21"]; if (i->is_number_float()) value.c2.y = (float)*i; else result = false;
	i = &a["22"]; if (i->is_number_float()) value.c2.z = (float)*i; else result = false;
	i = &a["23"]; if (i->is_number_float()) value.c2.w = (float)*i; else result = false;

	i = &a["30"]; if (i->is_number_float()) value.c3.x = (float)*i; else result = false;
	i = &a["31"]; if (i->is_number_float()) value.c3.y = (float)*i; else result = false;
	i = &a["32"]; if (i->is_number_float()) value.c3.z = (float)*i; else result = false;
	i = &a["33"]; if (i->is_number_float()) value.c3.w = (float)*i; else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, Aabb& value)
{
	GARDEN_ASSERT(!name.empty());
	if (beginChild(name))
	{
		auto min = (float3)value.getMin(), max = (float3)value.getMin();
		read("min", min);
		read("max", max);
		endChild();
		return value.trySet((f32x4)min, (f32x4)max);
	}
	return false;
}
bool JsonDeserializer::read(string_view name, f32x4& value, uint8 components)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(components <= 4);

	auto& object = *hierarchy.top();
	auto& a = object[name];
	if (!a.is_object())
		return false;

	auto result = true;
	constexpr const char* componentNames[4] = { "x", "y", "z", "w" };
	for (uint8 c = 0; c < components; c++)
	{
		auto i = &a[componentNames[c]]; 
		if (i->is_number_float()) value[c] = ((float)*i); else result = false;
	}
	return result;
}