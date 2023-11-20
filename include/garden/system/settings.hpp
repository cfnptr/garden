//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "ecsm.hpp"
#include "math/types.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
class SettingsSystem final : public System
{
public:
	enum class Type : uint32
	{
		Int, Float, Bool, String, Count
	};
	struct Item final
	{
		uint64 data = 0;
		Type type = {};

		Item(Type _type, uint64 _data) :
			type(_type), data(_data) { }
	};
private:
	void* confReader = nullptr;
	map<string, Item> items;

	SettingsSystem();
	~SettingsSystem() final;
	void terminate() final;
	
	friend class ecsm::Manager;
	friend class SettingsEditor;
public:
	void getInt(const string& name, int64& value);
	void getFloat(const string& name, double& value);
	void getBool(const string& name, bool& value);
	void getString(const string& name, string& value);

	void getInt(const string& name, int32& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int32)intValue;
	}
	void getInt(const string& name, uint32& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint32)intValue;
	}
	void getInt(const string& name, int16& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int16)intValue;
	}
	void getInt(const string& name, uint16& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint16)intValue;
	}
	void getInt(const string& name, int8& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int8)intValue;
	}
	void getInt(const string& name, uint8& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint8)intValue;
	}
	void getFloat(const string& name, float& value)
	{
		auto floatValue = (double)value;
		getFloat(name, floatValue);
		value = (float)floatValue;
	}

	void setInt(const string& name, int64 value);
	void setFloat(const string& name, double value);
	void setBool(const string& name, bool value);
	void setString(const string& name, string_view value);

	void setInt(const string& name, int32 value) { setInt(name, (int64)value); }
	void setInt(const string& name, uint32 value) { setInt(name, (int64)value); }
	void setInt(const string& name, int16 value) { setInt(name, (int64)value); }
	void setInt(const string& name, uint16 value) { setInt(name, (int64)value); }
	void setInt(const string& name, int8 value) { setInt(name, (int64)value); }
	void setInt(const string& name, uint8 value) { setInt(name, (int64)value); }
	void setFloat(const string& name, float value) { setFloat(name, (double)value); }
};

} // garden