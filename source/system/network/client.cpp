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

#include "garden/system/network/client.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"

using namespace garden;

void ClientNetworkSystem::onConnectionResult(NetsResult result)
{
	if (onConnection)
		onConnection(result);

	if (result == SUCCESS_NETS_RESULT)
		GARDEN_LOG_INFO("Connected to the server.");
	else
		GARDEN_LOG_WARN("Failed to connect to the server. (reason: " + string(netsResultToString(result)) + ")");
}
bool ClientNetworkSystem::onStreamReceive(const uint8_t* receiveBuffer, size_t byteCount)
{
	auto reason = handleStreamMessage(receiveBuffer, byteCount, messageBuffer, 
		messageBufferSize, &messageByteCount, messageLengthSize, onMessageReceive, this);
	if (reason != SUCCESS_NETS_RESULT)
	{
		GARDEN_LOG_ERROR("Failed to process server response. (reason: " + reasonToString(reason) + ")");
		return false;
	}
	return true;
}
bool ClientNetworkSystem::onDatagramReceive(const uint8_t* receiveBuffer, size_t byteCount)
{
	SET_CPU_ZONE_SCOPED("On Datagram Receive");

	if (byteCount < sizeof(uint32) + sizeof(uint8) * 3)
	{
		GARDEN_LOG_ERROR("Bad server datagram size. (byteCount: " + to_string(byteCount) + ")");
		return false;
	}

	::StreamMessage message;
	if (isSecure())
	{
		abort(); // TODO: decrypt received datagram.

		if (byteCount < sizeof(uint32) + sizeof(uint8) * 3)
		{
			GARDEN_LOG_ERROR("Bad server datagram size. (byteCount: " + to_string(byteCount) + ")");
			return false;
		}
	}

	uint32 datagramIndex;
	if (readStreamMessageUint32(&message, &datagramIndex))
	{
		GARDEN_LOG_ERROR("Bad server datagram data.");
		return false;
	}

	if (datagramIndex <= this->datagramIndex)
		return true; // TODO: handle uint32 overflow.
	this->datagramIndex = datagramIndex;

	return onMessageReceive(message, this);
}
int ClientNetworkSystem::onMessageReceive(::StreamMessage message, void* argument)
{
	SET_CPU_ZONE_SCOPED("On Response Receive");

	bool isSystem;
	if (readStreamMessageBool(&message, &isSystem))
		return BAD_DATA_NETS_RESULT;

	const void* typeString; psize typeLength;
	if (readStreamMessageData(&message, &typeString, &typeLength, sizeof(uint8)))
		return BAD_DATA_NETS_RESULT;
	string_view messageType((const char*)typeString, typeLength);

	auto clientSystem = (ClientNetworkSystem*)argument;
	if (isSystem)
	{
		auto result = clientSystem->networkables.find(messageType);
		if (result != clientSystem->networkables.end())
			return result->second->onResponse(message);
	}
	else
	{
		auto result = clientSystem->listeners.find(messageType);
		if (result != clientSystem->listeners.end())
			return result->second(message);
	}
	return BAD_DATA_NETS_RESULT;
}

//**********************************************************************************************************************
ClientNetworkSystem::ClientNetworkSystem(size_t receiveBufferSize, 
	size_t messageBufferSize, double timeoutTime, bool setSingleton)
	: 
	Singleton(setSingleton), nets::IStreamClient(receiveBufferSize, timeoutTime)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ClientNetworkSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", ClientNetworkSystem::preDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ClientNetworkSystem::update);

	if (messageBufferSize <= UINT8_MAX) messageLengthSize = sizeof(uint8);
	else if (messageBufferSize <= UINT16_MAX) messageLengthSize = sizeof(uint16);
	else if (messageBufferSize <= UINT32_MAX) messageLengthSize = sizeof(uint32);
	else if (messageBufferSize <= UINT64_MAX) messageLengthSize = sizeof(uint64);
	else abort();

	this->messageBufferSize = messageBufferSize + messageLengthSize;
	GARDEN_ASSERT(this->messageBufferSize <= receiveBufferSize);
	this->messageBuffer = new uint8[this->messageBufferSize];
}
ClientNetworkSystem::~ClientNetworkSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ClientNetworkSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", ClientNetworkSystem::preDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ClientNetworkSystem::update);

		delete[] messageBuffer;
	}
	unsetSingleton();
}

void ClientNetworkSystem::preInit()
{
	if (!isNetworkInitialized())
		throw GardenError("Failed to initialize network subsystems.");

	auto systemGroup = Manager::Instance::get()->tryGetSystemGroup<INetworkable>();
	if (systemGroup)
	{
		for (auto system : *systemGroup)
		{
			auto networkableSystem = dynamic_cast<INetworkable*>(system);
			auto messageType = networkableSystem->getMessageType();
			GARDEN_ASSERT(messageType.length() <= UINT8_MAX);
			GARDEN_ASSERT(listeners.find(messageType) == listeners.end());
			auto result = networkables.emplace(messageType, networkableSystem);
			GARDEN_ASSERT_MSG(result.second, "Already registered network message type");
		}
	}
}
void ClientNetworkSystem::preDeinit()
{
	destroy();
}

NetsResult ClientNetworkSystem::processEncKey(StreamRequest message) noexcept
{
	// TODO: use datagram encryption key.

	auto manager = Manager::Instance::get();
	manager->lock();
	manager->unlock();
	return SUCCESS_NETS_RESULT;
}