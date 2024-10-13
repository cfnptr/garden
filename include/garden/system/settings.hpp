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

/***********************************************************************************************************************
 * @file
 * @brief Settings storage functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "math/color.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;

/**
 * @brief Application settings holder.
 * @details The system responsible for saving and loading settings between program runs.
 */
class SettingsSystem final : public System, public Singleton<SettingsSystem>
{
	enum class Type : uint32
	{
		Int, Float, Bool, String, Color, Count
	};

	struct Item final
	{
		uint64 data = 0;
		Type type = {};

		Item(Type type, uint64 data) : data(data), type(type) { }
	};

	void* confReader = nullptr;
	unordered_map<string, Item> items;

	/**
	 * @brief Creates a new settings system instance.
	 * @param setSingleton set system singleton instance
	 */
	SettingsSystem(bool setSingleton = true);
	/**
	 * @brief Destroys settings system instance.
	 */
	~SettingsSystem() final;

	void preInit();
	void postDeinit();
	
	friend class ecsm::Manager;
	friend class SettingsEditor;
public:
	/**
	 * @brief Returns settings integer value. (int64)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, int64& value);
	/**
	 * @brief Returns settings floating value. (double)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getFloat(const string& name, double& value);
	/**
	 * @brief Returns settings boolean value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getBool(const string& name, bool& value);
	/**
	 * @brief Returns settings string value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getString(const string& name, string& value);
	/**
	 * @brief Returns settings color value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getColor(const string& name, Color& value);

	/**
	 * @brief Returns settings integer value. (int32)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, int32& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int32)intValue;
	}
	/**
	 * @brief Returns settings integer value. (uint32)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, uint32& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint32)intValue;
	}
	/**
	 * @brief Returns settings integer value. (int16)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, int16& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int16)intValue;
	}
	/**
	 * @brief Returns settings integer value. (uint16)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, uint16& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint16)intValue;
	}
	/**
	 * @brief Returns settings integer value. (int8)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, int8& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (int8)intValue;
	}
	/**
	 * @brief Returns settings integer value. (uint8)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getInt(const string& name, uint8& value)
	{
		auto intValue = (int64)value;
		getInt(name, intValue);
		value = (uint8)intValue;
	}

	/**
	 * @brief Returns settings floating value. (float)
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getFloat(const string& name, float& value)
	{
		auto floatValue = (double)value;
		getFloat(name, floatValue);
		value = (float)floatValue;
	}

	/**
	 * @brief Returns settings color value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getColor(const string& name, float4& value)
	{
		auto colorValue = (Color)value;
		getColor(name, colorValue);
		value = (float4)colorValue;
	}
	/**
	 * @brief Returns settings color value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getColor(const string& name, float3& value)
	{
		auto colorValue = (Color)value;
		getColor(name, colorValue);
		value = (float3)colorValue;
	}
	/**
	 * @brief Returns settings color value.
	 * @param[in] name target setting name
	 * @param[out] value reference to the setting value
	 * @return Setting value if exists, otherwise adds and returns initial value.
	 */
	void getColor(const string& name, float2& value)
	{
		auto colorValue = (Color)value;
		getColor(name, colorValue);
		value = (float2)colorValue;
	}

	/**
	 * @brief Sets settings integer value. (int64)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, int64 value);
	/**
	 * @brief Sets settings floating value. (double)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setFloat(const string& name, double value);
	/**
	 * @brief Sets settings boolean value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setBool(const string& name, bool value);
	/**
	 * @brief Sets settings string value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setString(const string& name, string_view value);
	/**
	 * @brief Sets settings color value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setColor(const string& name, Color value);

	/**
	 * @brief Sets settings integer value. (int32)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, int32 value) { setInt(name, (int64)value); }
	/**
	 * @brief Sets settings integer value. (uint32)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, uint32 value) { setInt(name, (int64)value); }
	/**
	 * @brief Sets settings integer value. (int16)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, int16 value) { setInt(name, (int64)value); }
	/**
	 * @brief Sets settings integer value. (uint16)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, uint16 value) { setInt(name, (int64)value); }
	/**
	 * @brief Sets settings integer value. (int8)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, int8 value) { setInt(name, (int64)value); }
	/**
	 * @brief Sets settings integer value. (uint8)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setInt(const string& name, uint8 value) { setInt(name, (int64)value); }

	/**
	 * @brief Sets settings integer value. (float)
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setFloat(const string& name, float value) { setFloat(name, (double)value); }

	/**
	 * @brief Sets settings color value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setColor(const string& name, const float4& value) { setColor(name, (Color)value); }
	/**
	 * @brief Sets settings color value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setColor(const string& name, const float3& value) { setColor(name, (Color)value); }
	/**
	 * @brief Sets settings color value.
	 * @param[in] name target setting name
	 * @param value setting value
	 */
	void setColor(const string& name, float2 value) { setColor(name, (Color)value); }
};

} // namespace garden