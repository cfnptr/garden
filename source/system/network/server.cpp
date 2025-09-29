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

#include "garden/system/network/server.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"

using namespace garden;

StreamServerHandle::StreamServerHandle(ServerSystem* serverSystem, SocketFamily socketFamily, 
	const char* service, size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, 
	size_t messageBufferSize, double timeoutTime, nets::SslContextView sslContext)
	:
	nets::IStreamServer(socketFamily, service, sessionBufferSize, 
		connectionQueueSize, receiveBufferSize, timeoutTime, sslContext),
	serverSystem(serverSystem)
{
	if (messageBufferSize <= UINT8_MAX)
		messageLengthSize = sizeof(uint8);
	else if (messageBufferSize <= UINT16_MAX)
		messageLengthSize = sizeof(uint16);
	else if (messageBufferSize <= UINT32_MAX)
		messageLengthSize = sizeof(uint32);
	else if (messageBufferSize <= UINT64_MAX)
		messageLengthSize = sizeof(uint64);
	else abort();

	this->messageBufferSize = messageBufferSize + messageLengthSize;
}

bool StreamServerHandle::onSessionCreate(nets::StreamSessionView streamSession, void*& handle)
{
	SET_CPU_ZONE_SCOPED("On Server Session Create");

	ClientSession* clientSession;
	if (serverSystem->onSessionCreate)
	{
		clientSession = nullptr;
		auto reason = serverSystem->onSessionCreate(streamSession, clientSession);
		if (reason != SUCCESS_NETS_RESULT)
		{
			auto reasonString = reason >= NETS_RESULT_COUNT ? to_string(reason) : netsResultToString(reason);
			GARDEN_LOG_INFO("Rejected client session. (reason: " + reasonString + ")");
			return false;
		}
	}
	else
	{
		clientSession = new ClientSession();
	}

	clientSession->streamSession = streamSession.getInstance();
	clientSession->messageBuffer = new uint8[messageBufferSize];
	handle = clientSession;

	string ip, port; streamSession.getRemoteAddress().getHostService(ip, port);
	GARDEN_LOG_INFO("Created a new client session. (ip: " + ip + ", port:" + port + ")");
	return true;
}
void StreamServerHandle::onSessionDestroy(nets::StreamSessionView streamSession, int reason)
{
	SET_CPU_ZONE_SCOPED("On Server Session Destroy");

	auto clientSession = (ClientSession*)streamSession.getHandle();
	delete[] clientSession->messageBuffer;
	clientSession->messageBuffer = nullptr;

	if (serverSystem->onSessionDestroy)
		serverSystem->onSessionDestroy(clientSession, reason);
	else
	 	delete clientSession;

	string ip, port; streamSession.getRemoteAddress().getHostService(ip, port);
	auto reasonString = reason >= NETS_RESULT_COUNT ? to_string(reason) : netsResultToString(reason);
	GARDEN_LOG_INFO("Destroyed client session. (ip: " + ip + ", port:" + port + ", reason: " + reasonString + ")");
}

//**********************************************************************************************************************
int StreamServerHandle::onStreamReceive(nets::StreamSessionView streamSession, 
	const uint8_t* receiveBuffer, size_t byteCount)
{
	auto clientSession = (ClientSession*)streamSession.getHandle();
	std::pair<ServerSystem*, ClientSession*> pair = { serverSystem, clientSession };
	return handleStreamMessage(receiveBuffer, byteCount, clientSession->messageBuffer, messageBufferSize, 
		&clientSession->messageByteCount, messageLengthSize, onMessageReceive, &clientSession);
}
int StreamServerHandle::onMessageReceive(StreamMessage streamMessage, void* argument)
{
	SET_CPU_ZONE_SCOPED("On Server Message Receive");

	const uint8* data;
	if (readStreamMessage(&streamMessage, &data, sizeof(uint8)))
		return BAD_DATA_NETS_RESULT;
	auto typeLength = *data;

	if (readStreamMessage(&streamMessage, &data, typeLength))
		return BAD_DATA_NETS_RESULT;
	string_view messageType((const char*)data, typeLength);

	auto pair = *((std::pair<ServerSystem*, ClientSession*>*)argument);
	if (!pair.second->isAuthorized && messageType != "a")
		return BAD_DATA_NETS_RESULT;
	
	streamMessage.buffer = streamMessage.buffer + streamMessage.offset;
	streamMessage.size = streamMessage.size - streamMessage.offset;
	streamMessage.offset = 0;
	{
		auto result = pair.first->networkables.find(messageType);
		if (result != pair.first->networkables.end())
		{
			result->second->onRequest(pair.second, streamMessage);
			return SUCCESS_NETS_RESULT;
		}
	}

	auto result = pair.first->listeners.find(messageType);
	if (result != pair.first->listeners.end())
	{
		result->second(pair.second, streamMessage);
		return SUCCESS_NETS_RESULT;
	}

	return BAD_DATA_NETS_RESULT;
}

//**********************************************************************************************************************
ServerSystem::ServerSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ServerSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ServerSystem::update);

	listeners.emplace("a", [this](ClientSession* session, nets::StreamMessage message)
	{
		if (session->isAuthorized)
			return (int)BAD_DATA_NETS_RESULT;

		if (onSessionAuthorize)
		{
			auto result = onSessionAuthorize(session, message);
			if (result != 0)
				return result;
		}
		if (streamServer.isSecure())
		{
			// TODO: UDP packets encryption key.
		}
		return (int)SUCCESS_NETS_RESULT;
	});
}
ServerSystem::~ServerSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ServerSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ServerSystem::update);
	}
	unsetSingleton();
}

void ServerSystem::preInit()
{
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
void ServerSystem::update()
{
	if (streamServer.getInstance())
	{
		if (!streamServer.isRunning())
			throw GardenError("Stream server has stopped.");
	}
}

void ServerSystem::start(SocketFamily socketFamily, const char* service, 
	size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, 
	size_t messageBufferSize, double timeoutTime, nets::SslContextView sslContext)
{
	GARDEN_ASSERT(service);
	GARDEN_ASSERT(sessionBufferSize > 0);
	GARDEN_ASSERT(connectionQueueSize > 0);
	GARDEN_ASSERT(receiveBufferSize > 0);
	GARDEN_ASSERT(messageBufferSize > 0);
	GARDEN_ASSERT(timeoutTime > 0);
	GARDEN_ASSERT(!streamServer.getInstance());

	streamServer = StreamServerHandle(this, socketFamily, service, sessionBufferSize, 
		connectionQueueSize, receiveBufferSize, messageBufferSize, timeoutTime, sslContext);

	if (!streamServer.isSecure())
		GARDEN_LOG_WARN("Server messages are not encrypted!");
	GARDEN_LOG_INFO("Started server.");
}
void ServerSystem::stop()
{
	if (!streamServer.getInstance())
		return;

	if (streamServer.isRunning())
		GARDEN_LOG_INFO("Stopping server...");
	else
		GARDEN_LOG_WARN("Server is not running.");
	streamServer = {};
}