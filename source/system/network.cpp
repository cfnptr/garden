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

#include "garden/system/network.hpp"
#include "nets/socket.hpp"

using namespace garden;

//**********************************************************************************************************************
NetworkSystem::NetworkSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", NetworkSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", NetworkSystem::postDeinit);
}
NetworkSystem::~NetworkSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);
		ECSM_SUBSCRIBE_TO_EVENT("PreInit", NetworkSystem::preInit);
		ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", NetworkSystem::postDeinit);
	}

	unsetSingleton();
}

void NetworkSystem::preInit()
{
	initializeNetwork();
}
void NetworkSystem::postDeinit()
{
	terminateNetwork();
}

void NetworkSystem::resetComponent(View<Component> component, bool full)
{
	if (full)
	{
		auto networkView = View<NetworkComponent>(component);
		networkView->isClientOwned = false;
	}
}
void NetworkSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<NetworkComponent>(source);
	auto destinationView = View<NetworkComponent>(destination);
	destinationView->isClientOwned = sourceView->isClientOwned;
}
string_view NetworkSystem::getComponentName() const
{
	return "Network";
}

//**********************************************************************************************************************
void NetworkSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto networkView = View<NetworkComponent>(component);
	serializer.write("isClientOwned", networkView->isClientOwned);
}
void NetworkSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto networkView = View<NetworkComponent>(component);
	deserializer.read("isClientOwned", networkView->isClientOwned);
}
