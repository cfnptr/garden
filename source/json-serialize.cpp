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

#include "garden/json-serialize.hpp"
#include "garden/utf.hpp"

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
	hierarchy.emplace(&hierarchy.top()->operator[](name));
}
void JsonSerializer::endChild()
{
	GARDEN_ASSERT_MSG(hierarchy.size() > 1, "No child to end");
	hierarchy.pop();
}

//**********************************************************************************************************************
void JsonSerializer::beginArrayElement()
{
	auto element = &hierarchy.top()->emplace_back(json());
	hierarchy.emplace(element);
}

void JsonSerializer::write(int64 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(uint64 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(int32 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(uint32 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(int16 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(uint16 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(int8 value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(uint8 value)
{
	*hierarchy.top() = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(bool value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(float value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(double value)
{
	*hierarchy.top() = value;
}
void JsonSerializer::write(string_view value)
{
	*hierarchy.top() = value;
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
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, uint64 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, int32 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, uint32 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, int16 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, uint16 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, int8 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, uint8 value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, bool value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, float value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, double value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, string_view value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = value;
}
void JsonSerializer::write(string_view name, u32string_view value)
{
	GARDEN_ASSERT(!name.empty());
	string value8; UTF::convert(value, value8);
	hierarchy.top()->operator[](name) = std::move(value8);
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, int2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y)
		object[name] = { { "x", value.x }, { "y", value.y } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, int3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, int4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z || value.x != value.w)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, uint2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y)
		object[name] = { { "x", value.x }, { "y", value.y } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, uint3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, uint4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z || value.x != value.w)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, float2 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y)
		object[name] = { { "x", value.x }, { "y", value.y } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, float3 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, float4 value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = *hierarchy.top();
	if (value.x != value.y || value.x != value.z || value.x != value.w)
		object[name] = { { "x", value.x }, { "y", value.y }, { "z", value.z }, { "w", value.w } };
	else object[name] = value.x;
}
void JsonSerializer::write(string_view name, quat value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) =
	{ { "x", value.getX() }, { "y", value.getY() }, { "z", value.getZ() }, { "w", value.getW() } };
}

//**********************************************************************************************************************
void JsonSerializer::write(string_view name, const float2x2& value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) =
	{
		{ "00", value.c0.x }, { "01", value.c0.y },
		{ "10", value.c1.x }, { "11", value.c1.y }
	};
}
void JsonSerializer::write(string_view name, const float3x3& value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) =
	{
		{ "00", value.c0.x }, { "01", value.c0.y }, { "02", value.c0.z },
		{ "10", value.c1.x }, { "11", value.c1.y }, { "12", value.c1.z },
		{ "20", value.c2.x }, { "21", value.c2.y }, { "22", value.c2.z }
	};
}
void JsonSerializer::write(string_view name, const float4x4& value)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) =
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
void JsonSerializer::write(string_view name, Color value, bool rgb)
{
	GARDEN_ASSERT(!name.empty());
	hierarchy.top()->operator[](name) = rgb ? 
		std::move(value.toHex3()) : std::move(value.toHex4());
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
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_structured())
		return false;
	hierarchy.emplace(&object);
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
	return hierarchy.top()->size();
}
bool JsonDeserializer::beginArrayElement(psize index)
{
	auto& object = *hierarchy.top();
	if (!object.is_array())
		return false;
	hierarchy.emplace(&object.at(index));
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
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (int64)object;
	return true;
}
bool JsonDeserializer::read(string_view name, uint64& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (uint64)object;
	return true;
}
bool JsonDeserializer::read(string_view name, int32& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (int32)object;
	return true;
}
bool JsonDeserializer::read(string_view name, uint32& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (uint32)object;
	return true;
}
bool JsonDeserializer::read(string_view name, int16& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (int16)object;
	return true;
}
bool JsonDeserializer::read(string_view name, uint16& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (uint16)object;
	return true;
}
bool JsonDeserializer::read(string_view name, int8& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (int8)object;
	return true;
}
bool JsonDeserializer::read(string_view name, uint8& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_integer())
		return false;
	value = (uint8)object;
	return true;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, bool& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_boolean())
		return false;
	value = (bool)object;
	return true;
}
bool JsonDeserializer::read(string_view name, float& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_float())
		return false;
	value = (float)object;
	return true;
}
bool JsonDeserializer::read(string_view name, double& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_number_float())
		return false;
	value = (double)object;
	return true;
}
bool JsonDeserializer::read(string_view name, string& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_string())
		return false;
	value.assign((const string&)object);
	return true;
}
bool JsonDeserializer::read(string_view name, u32string& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_string())
		return false;
	return UTF::convert((const string&)object, value) == 0;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, int2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_integer())
	{
		value = int2((int32)object);
		return true;
	}
	
	if (!object.is_object())
		return false;
	auto result = true;
	auto i = &object["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &object["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, int3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_integer())
	{
		value = int3((int32)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &object["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = false;
	i = &object["z"]; if (i->is_number_integer()) value.z = (int32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, int4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_integer())
	{
		value = int4((int32)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_integer()) value.x = (int32)*i; else result = false;
	i = &object["y"]; if (i->is_number_integer()) value.y = (int32)*i; else result = false;
	i = &object["z"]; if (i->is_number_integer()) value.z = (int32)*i; else result = false;
	i = &object["w"]; if (i->is_number_integer()) value.w = (int32)*i; else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, uint2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_unsigned())
	{
		value = uint2((uint32)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &object["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, uint3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_unsigned())
	{
		value = uint3((uint32)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &object["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = false;
	i = &object["z"]; if (i->is_number_unsigned()) value.z = (uint32)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, uint4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_unsigned())
	{
		value = uint4((uint32)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_unsigned()) value.x = (uint32)*i; else result = false;
	i = &object["y"]; if (i->is_number_unsigned()) value.y = (uint32)*i; else result = false;
	i = &object["z"]; if (i->is_number_unsigned()) value.z = (uint32)*i; else result = false;
	i = &object["w"]; if (i->is_number_unsigned()) value.w = (uint32)*i; else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, float2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_float())
	{
		value = float2((float)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &object["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_float())
	{
		value = float3((float)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &object["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	i = &object["z"]; if (i->is_number_float()) value.z = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_float())
	{
		value = float4((float)object);
		return true;
	}

	auto result = true;
	auto i = &object["x"]; if (i->is_number_float()) value.x = (float)*i; else result = false;
	i = &object["y"]; if (i->is_number_float()) value.y = (float)*i; else result = false;
	i = &object["z"]; if (i->is_number_float()) value.z = (float)*i; else result = false;
	i = &object["w"]; if (i->is_number_float()) value.w = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, quat& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_object())
		return false;

	auto result = true;
	auto i = &object["x"]; if (i->is_number_float()) value.setX((float)*i); else result = false;
	i = &object["y"]; if (i->is_number_float()) value.setY((float)*i); else result = false;
	i = &object["z"]; if (i->is_number_float()) value.setZ((float)*i); else result = false;
	i = &object["w"]; if (i->is_number_float()) value.setW((float)*i); else result = false;
	return result;
}

//**********************************************************************************************************************
bool JsonDeserializer::read(string_view name, float2x2& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_object())
		return false;

	auto result = true;
	auto i = &object["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &object["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;

	i = &object["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &object["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float3x3& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_object())
		return false;

	auto result = true;
	auto i = &object["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &object["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;
	i = &object["02"]; if (i->is_number_float()) value.c0.z = (float)*i; else result = false;

	i = &object["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &object["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	i = &object["12"]; if (i->is_number_float()) value.c1.z = (float)*i; else result = false;

	i = &object["20"]; if (i->is_number_float()) value.c2.x = (float)*i; else result = false;
	i = &object["21"]; if (i->is_number_float()) value.c2.y = (float)*i; else result = false;
	i = &object["22"]; if (i->is_number_float()) value.c2.z = (float)*i; else result = false;
	return result;
}
bool JsonDeserializer::read(string_view name, float4x4& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_object())
		return false;

	auto result = true;
	auto i = &object["00"]; if (i->is_number_float()) value.c0.x = (float)*i; else result = false;
	i = &object["01"]; if (i->is_number_float()) value.c0.y = (float)*i; else result = false;
	i = &object["02"]; if (i->is_number_float()) value.c0.z = (float)*i; else result = false;
	i = &object["03"]; if (i->is_number_float()) value.c0.w = (float)*i; else result = false;

	i = &object["10"]; if (i->is_number_float()) value.c1.x = (float)*i; else result = false;
	i = &object["11"]; if (i->is_number_float()) value.c1.y = (float)*i; else result = false;
	i = &object["12"]; if (i->is_number_float()) value.c1.z = (float)*i; else result = false;
	i = &object["13"]; if (i->is_number_float()) value.c1.w = (float)*i; else result = false;

	i = &object["20"]; if (i->is_number_float()) value.c2.x = (float)*i; else result = false;
	i = &object["21"]; if (i->is_number_float()) value.c2.y = (float)*i; else result = false;
	i = &object["22"]; if (i->is_number_float()) value.c2.z = (float)*i; else result = false;
	i = &object["23"]; if (i->is_number_float()) value.c2.w = (float)*i; else result = false;

	i = &object["30"]; if (i->is_number_float()) value.c3.x = (float)*i; else result = false;
	i = &object["31"]; if (i->is_number_float()) value.c3.y = (float)*i; else result = false;
	i = &object["32"]; if (i->is_number_float()) value.c3.z = (float)*i; else result = false;
	i = &object["33"]; if (i->is_number_float()) value.c3.w = (float)*i; else result = false;
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
bool JsonDeserializer::read(string_view name, Color& value)
{
	GARDEN_ASSERT(!name.empty());
	auto& object = hierarchy.top()->operator[](name);
	if (!object.is_string())
		return false;
	const string& hex = object;
	if (hex.length() != 6 && hex.length() != 8)
		return false;
	value = Color(hex);
	return true;
}
bool JsonDeserializer::read(string_view name, f32x4& value, uint8 components)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(components <= 4);

	auto& object = hierarchy.top()->operator[](name);
	if (object.is_number_float())
	{
		auto floatValue = (float)object;
		for (uint8 c = 0; c < components; c++)
			value[c] = floatValue;
		return true;
	}

	if (!object.is_object())
		return false;

	auto result = true;
	constexpr const char* componentNames[4] = { "x", "y", "z", "w" };
	for (uint8 c = 0; c < components; c++)
	{
		auto i = &object[componentNames[c]]; 
		if (i->is_number_float()) value[c] = ((float)*i); else result = false;
	}
	return result;
}