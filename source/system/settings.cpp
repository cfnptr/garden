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

#include "garden/system/settings.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/log.hpp"

#include "mpio/directory.hpp"
#include "nlohmann/json.hpp"
#include <fstream>

using namespace garden;
using json = nlohmann::json;

//**********************************************************************************************************************
SettingsSystem::SettingsSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", SettingsSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", SettingsSystem::postDeinit);
}

void SettingsSystem::preInit()
{
	auto appInfoSystem = AppInfoSystem::Instance::get();
	try
	{
		auto appDataPath = mpio::Directory::getAppDataPath(appInfoSystem->getAppDataName());
		std::ifstream inputStream(appDataPath / "settings.json");
		if (!inputStream.is_open())
			throw GardenError("File does not exist.");
		settings = new json(json::parse(inputStream));
		GARDEN_LOG_INFO("Loaded settings file.");
	}
	catch (const exception& e)
	{
		GARDEN_LOG_WARN("Failed to load settings file. (error: " + string(e.what()) + ")");
	}
}
void SettingsSystem::postDeinit()
{
	auto appInfoSystem = AppInfoSystem::Instance::get();
	try
	{
		json data;
		data["app"] = appInfoSystem->getName();
		data["version"] = appInfoSystem->getVersion().toString3();
		auto& settings = data["settings"];

		string hex;
		for (const auto& pair : items)
		{
			switch (pair.second.type)
			{
			case Type::Int: settings[pair.first] = *((int64*)&pair.second.data); break;
			case Type::Float: settings[pair.first] = *((double*)&pair.second.data); break;
			case Type::Bool: settings[pair.first] = *((bool*)&pair.second.data); break;
			case Type::String: settings[pair.first] = *((const char**)&pair.second.data); break;
			case Type::Color: settings[pair.first] = Color((uint32)pair.second.data).toHex4(); break;
			default: abort();
			}
		}

		auto appDataPath = mpio::Directory::getAppDataPath(appInfoSystem->getAppDataName());
		std::ofstream fileStream(appDataPath / "settings.json");
		fileStream << std::setw(1) << std::setfill('\t') << data;

		GARDEN_LOG_INFO("Stored settings file.");
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to store settings file. (error: " + string(e.what()) + ")");
	}
}

//**********************************************************************************************************************
void SettingsSystem::getInt(const string& name, int64& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (settings)
		{
			auto& data = (*((json*)settings))[name];
			if (data.is_number_integer())
				value = (int64)data;
		}
		items.emplace(name, Item(Type::Int, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Int, 
		"Incorrect setting [" + string(name) + "] type");
	value = *((int64*)&searchResult->second.data);
}

void SettingsSystem::getFloat(const string& name, double& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (settings)
		{
			auto& data = (*((json*)settings))[name];
			if (data.is_number_float())
				value = (double)data;
		}
		items.emplace(name, Item(Type::Float, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Float, 
		"Incorrect setting [" + string(name) + "] type");
	value = *((double*)&searchResult->second.data);
}

void SettingsSystem::getBool(const string& name, bool& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (settings)
		{
			auto& data = (*((json*)settings))[name];
			if (data.is_boolean())
				value = (bool)data;
		}
		items.emplace(name, Item(Type::Bool, value));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Bool, 
		"Incorrect setting [" + string(name) + "] type");
	value = searchResult->second.data;
}

void SettingsSystem::getString(const string& name, string& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (settings)
		{
			auto& data = (*((json*)settings))[name];
			if (data.is_string())
				value = (string)data;
		}

		auto instance = new char[value.length() + 1];
		memcpy(instance, value.c_str(), value.length());
		instance[value.length()] = '\0';
		items.emplace(name, Item(Type::String, *((uint64*)&instance)));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::String, 
		"Incorrect setting [" + string(name) + "] type");
	value = string(*((const char**)&searchResult->second.data));
}

void SettingsSystem::getColor(const string& name, Color& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (settings)
		{
			auto& data = (*((json*)settings))[name];
			if (data.is_string())
				value = Color((const string&)data);
		}
		items.emplace(name, Item(Type::Color, (uint32)value));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Color, 
		"Incorrect setting [" + string(name) + "] type");
	value = Color((uint32)searchResult->second.data);
}

//**********************************************************************************************************************
void SettingsSystem::setInt(const string& name, int64 value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::Int, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Int, 
		"Incorrect setting [" + string(name) + "] type");
	*((int64*)&searchResult->second.data) = value;
}

void SettingsSystem::setFloat(const string& name, double value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::Float, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Float, 
		"Incorrect setting [" + string(name) + "] type");
	*((double*)&searchResult->second.data) = value;
}

void SettingsSystem::setBool(const string& name, bool value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::Bool, value));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Bool, 
		"Incorrect setting [" + string(name) + "] type");
	searchResult->second.data = value;
}

void SettingsSystem::setString(const string& name, string_view value)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(!value.empty());

	auto instance = new char[value.length() + 1];
	memcpy(instance, value.data(), value.length());
	instance[value.length()] = '\0';

	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::String, *((uint64*)&instance)));
		return;
	}

	GARDEN_ASSERT_MSG(searchResult->second.type == Type::String, 
		"Incorrect setting [" + string(name) + "] type");
	delete *((char**)&searchResult->second.data);
	*((char**)&searchResult->second.data) = instance;
}

void SettingsSystem::setColor(const string& name, Color value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::Color, (uint32)value));
		return;
	}
	GARDEN_ASSERT_MSG(searchResult->second.type == Type::Color, 
		"Incorrect setting [" + string(name) + "] type");
	searchResult->second.data = (uint32)value;
}