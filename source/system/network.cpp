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

#include "garden/system/network.hpp"
#include "garden/system/log.hpp"
#include "nets/socket.hpp"

using namespace garden;

//**********************************************************************************************************************
void NetworkComponent::setClientUID(string_view uid)
{
	delete[] clientUID;

	if (uid.empty())
	{
		clientUID = nullptr;
	}
	else
	{
		clientUID = new char[uid.size() + 1];
		memcpy(clientUID, uid.data(), uid.size());
		clientUID[uid.size()] = '\0';
	}
}
bool NetworkComponent::trySetEntityUID(uint32 uid)
{
	if (entityUID == uid)
		return true;

	auto& entityMap = NetworkSystem::Instance::get()->entityMap;
	if (uid)
	{
		if (!entityMap.emplace(uid, entity).second)
			return false;
	}
	if (entityUID)
	{
		auto result = entityMap.erase(entityUID);
		GARDEN_ASSERT_MSG(result == 1, "Detected memory corruption");
	}

	entityUID = uid;
	return true;
}

//**********************************************************************************************************************
NetworkSystem::NetworkSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);

	if (!initializeNetwork())
		GardenError("Failed to initialize network subsystems.");
}
NetworkSystem::~NetworkSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);

	terminateNetwork();
	unsetSingleton();
}

void NetworkSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<NetworkComponent>(component);
	delete[] componentView->clientUID; componentView->clientUID = nullptr;
	componentView->setEntityUID(0);

	if (full)
		**componentView = {};
}
void NetworkSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<NetworkComponent>(source);
	auto destinationView = View<NetworkComponent>(destination);
	destinationView->isClientOwned = sourceView->isClientOwned;
	
	if (sourceView->clientUID)
		destinationView->setClientUID(sourceView->clientUID);
}
string_view NetworkSystem::getComponentName() const
{
	return "Network";
}

//**********************************************************************************************************************
void NetworkSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<NetworkComponent>(component);
	if (componentView->isClientOwned)
		serializer.write("isClientOwned", true);
	if (componentView->clientUID)
		serializer.write("clientUID", componentView->clientUID);
	if (componentView->entityUID)
		serializer.write("entityUID", componentView->entityUID);
}
void NetworkSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<NetworkComponent>(component); 
	deserializer.read("isClientOwned", componentView->isClientOwned);
	if (deserializer.read("clientUID", valueStringCache))
		componentView->setClientUID(valueStringCache);

	if (deserializer.read("entityUID", componentView->entityUID))
	{
		auto result = entityMap.emplace(componentView->entityUID, componentView->entity);
		if (!result.second)
		{
			GARDEN_LOG_ERROR("Deserialized entity with already existing "
				"network UID. (UID: " + to_string(componentView->entityUID) + ")");
			componentView->entityUID = {};
		}
	}
}