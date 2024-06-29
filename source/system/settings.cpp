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

#include "garden/system/settings.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/log.hpp"

#include "conf/reader.hpp"
#include "conf/writer.hpp"
#include "mpio/directory.hpp"
#include <iostream>

using namespace mpio;
using namespace garden;

//**********************************************************************************************************************
SettingsSystem::SettingsSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("PreInit", SettingsSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostDeinit", SettingsSystem::postDeinit);
}
SettingsSystem::~SettingsSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", SettingsSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", SettingsSystem::postDeinit);
	}

	delete (conf::Reader*)confReader;

	for (const auto& pair : items)
	{
		if (pair.second.type == Type::String)
			delete (char*)pair.second.data;
	}
}

//**********************************************************************************************************************
void SettingsSystem::preInit()
{
	auto appInfoSystem = AppInfoSystem::getInstance();
	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();

	try
	{
		auto appDataPath = Directory::getAppDataPath(appInfoSystem->getAppDataName());
		confReader = new conf::Reader(appDataPath / "settings.txt");
		if (logSystem)
			logSystem->info("Loaded settings file.");
	}
	catch (const exception& e)
	{
		if (logSystem)
			logSystem->warn("Failed to load settings file. (error: " + string(e.what()) + ")");
	}
}
void SettingsSystem::postDeinit()
{
	auto appInfoSystem = AppInfoSystem::getInstance();
	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();

	try
	{
		auto appDataPath = Directory::getAppDataPath(appInfoSystem->getAppDataName());
		conf::Writer confWriter(appDataPath / "settings.txt");

		confWriter.writeComment(appInfoSystem->getName() +
			" Settings (v" + appInfoSystem->getVersion().toString3() + ")");
		confWriter.writeNewLine();

		string hex;
		for (const auto& pair : items)
		{
			switch (pair.second.type)
			{
			case Type::Int: confWriter.write(pair.first, *((int64*)&pair.second.data)); break;
			case Type::Float: confWriter.write(pair.first, *((double*)&pair.second.data)); break;
			case Type::Bool: confWriter.write(pair.first, *((bool*)&pair.second.data)); break;
			case Type::String: confWriter.write(pair.first, *((const char*)&pair.second.data)); break;
			case Type::Color:
				hex = Color((uint32)pair.second.data).toHex();
				confWriter.write(pair.first, hex);
				break;
			default: abort();
			}
		}

		if (logSystem)
			logSystem->info("Stored settings file.");
	}
	catch (const exception& e)
	{
		if (logSystem)
			logSystem->error("Failed to store settings file. (error: " + string(e.what()) + ")");
	}
}

//**********************************************************************************************************************
void SettingsSystem::getInt(const string& name, int64& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (confReader)
			((conf::Reader*)confReader)->get(name, value);
		items.emplace(name, Item(Type::Int, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT(searchResult->second.type == Type::Int);
	value = *((int64*)&searchResult->second.data);
}

void SettingsSystem::getFloat(const string& name, double& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (confReader)
			((conf::Reader*)confReader)->get(name, value);
		items.emplace(name, Item(Type::Float, *((uint64*)&value)));
		return;
	}
	GARDEN_ASSERT(searchResult->second.type == Type::Float);
	value = *((double*)&searchResult->second.data);
}

void SettingsSystem::getBool(const string& name, bool& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (confReader)
			((conf::Reader*)confReader)->get(name, value);
		items.emplace(name, Item(Type::Bool, value));
		return;
	}
	GARDEN_ASSERT(searchResult->second.type == Type::Bool);
	value = searchResult->second.data;
}

void SettingsSystem::getString(const string& name, string& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (confReader)
		{
			string_view stringView;
			auto result = ((conf::Reader*)confReader)->get(name, stringView);
			if (result)
				value = string(stringView);
		}

		auto instance = new char[value.length() + 1];
		memcpy(instance, value.c_str(), value.length());
		instance[value.length()] = '\0';
		items.emplace(name, Item(Type::String, *((uint64*)&instance)));
		return;
	}
	GARDEN_ASSERT(searchResult->second.type == Type::String);
	value = string(*((const char**)&searchResult->second.data));
}

void SettingsSystem::getColor(const string& name, Color& value)
{
	GARDEN_ASSERT(!name.empty());
	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		if (confReader)
		{
			string_view stringView;
			auto result = ((conf::Reader*)confReader)->get(name, stringView);
			if (result && (stringView.length() == 8 || stringView.length() == 6))
				value = Color(string(stringView));
		}
		items.emplace(name, Item(Type::Color, (uint32)value));
		return;
	}
	GARDEN_ASSERT(searchResult->second.type == Type::Color);
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
	GARDEN_ASSERT(searchResult->second.type == Type::Int);
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
	GARDEN_ASSERT(searchResult->second.type == Type::Float);
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
	GARDEN_ASSERT(searchResult->second.type == Type::Bool);
	searchResult->second.data = value;
}

void SettingsSystem::setString(const string& name, string_view value)
{
	GARDEN_ASSERT(!name.empty());
	auto instance = new char[value.length() + 1];
	memcpy(instance, value.data(), value.length());
	instance[value.length()] = '\0';

	auto searchResult = items.find(name);
	if (searchResult == items.end())
	{
		items.emplace(name, Item(Type::String, *((uint64*)&instance)));
		return;
	}

	GARDEN_ASSERT(searchResult->second.type == Type::String);
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
	GARDEN_ASSERT(searchResult->second.type == Type::Color);
	searchResult->second.data = (uint32)value;
}