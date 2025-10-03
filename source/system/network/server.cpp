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
#include "garden/system/thread.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"

using namespace garden;

StreamServerHandle::StreamServerHandle(ServerNetworkSystem* serverSystem, SocketFamily socketFamily, 
	const char* service, size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, 
	size_t messageBufferSize, double timeoutTime, nets::SslContextView sslContext)
	:
	nets::IStreamServer(socketFamily, service, sessionBufferSize, 
		connectionQueueSize, receiveBufferSize, timeoutTime, sslContext),
	serverSystem(serverSystem)
{
	if (messageBufferSize <= UINT8_MAX) messageLengthSize = sizeof(uint8);
	else if (messageBufferSize <= UINT16_MAX) messageLengthSize = sizeof(uint16);
	else if (messageBufferSize <= UINT32_MAX) messageLengthSize = sizeof(uint32);
	else if (messageBufferSize <= UINT64_MAX) messageLengthSize = sizeof(uint64);
	else abort();

	this->messageBufferSize = messageBufferSize + messageLengthSize;
	GARDEN_ASSERT(this->messageBufferSize <= receiveBufferSize);
}

bool StreamServerHandle::onSessionCreate(nets::StreamSessionView streamSession, void*& handle)
{
	SET_CPU_ZONE_SCOPED("On Server Session Create");

	streamSession.getSocket().setNoDelay(true);

	ClientSession* clientSession;
	if (serverSystem->onSessionCreate)
	{
		clientSession = nullptr;
		auto reason = serverSystem->onSessionCreate(streamSession, clientSession);
		if (reason != SUCCESS_NETS_RESULT)
		{
			GARDEN_LOG_INFO("Rejected client session. (address: " + 
				streamSession.getAddress() + ", reason: " + reasonToString(reason) + ")");
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

	GARDEN_LOG_INFO("Created a new client session. (address: " + streamSession.getAddress() + ")");
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

	GARDEN_LOG_INFO("Destroyed client session. (address: " + 
		streamSession.getAddress() + ", " "reason: " + reasonToString(reason) + ")");
}

//**********************************************************************************************************************
int StreamServerHandle::onStreamReceive(nets::StreamSessionView streamSession, 
	const uint8_t* receiveBuffer, size_t byteCount)
{
	auto clientSession = (ClientSession*)streamSession.getHandle();
	std::pair<ServerNetworkSystem*, ClientSession*> pair = { serverSystem, clientSession };
	return handleStreamMessage(receiveBuffer, byteCount, clientSession->messageBuffer, messageBufferSize, 
		&clientSession->messageByteCount, messageLengthSize, onMessageReceive, &pair);
}
int StreamServerHandle::onMessageReceive(::StreamMessage streamMessage, void* argument)
{
	SET_CPU_ZONE_SCOPED("On Request Receive");

	bool isSystem;
	if (readStreamMessageBool(&streamMessage, &isSystem))
		return BAD_DATA_NETS_RESULT;

	const char* typeString; psize typeLength;
	if (readStreamMessageString(&streamMessage, &typeString, &typeLength, sizeof(uint8)))
		return BAD_DATA_NETS_RESULT;
	string_view messageType(typeString, typeLength);

	auto pair = *((std::pair<ServerNetworkSystem*, ClientSession*>*)argument);
	if (!pair.second->isAuthorized && messageType != "a")
		return BAD_DATA_NETS_RESULT;
	
	if (isSystem)
	{
		auto result = pair.first->networkables.find(messageType);
		if (result != pair.first->networkables.end())
		{
			result->second->onRequest(pair.second, streamMessage);
			return SUCCESS_NETS_RESULT;
		}
	}
	else
	{
		auto result = pair.first->listeners.find(messageType);
		if (result != pair.first->listeners.end())
		{
			result->second(pair.second, streamMessage);
			return SUCCESS_NETS_RESULT;
		}
	}
	return BAD_DATA_NETS_RESULT;
}

//**********************************************************************************************************************
ServerNetworkSystem::ServerNetworkSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ServerNetworkSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ServerNetworkSystem::update);

	listeners.emplace("a", [this](ClientSession* session, StreamRequest message)
	{
		if (session->isAuthorized)
			return (int)BAD_DATA_NETS_RESULT;

		if (onSessionAuthorize)
		{
			auto result = onSessionAuthorize(session, message);
			if (result != SUCCESS_NETS_RESULT)
				return result;
		}
		if (streamServer->isSecure())
		{
			// TODO: UDP packets encryption key.
		}
		return (int)SUCCESS_NETS_RESULT;
	});
}
ServerNetworkSystem::~ServerNetworkSystem()
{
	stop();

	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ServerNetworkSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ServerNetworkSystem::update);
	}
	unsetSingleton();
}

void ServerNetworkSystem::preInit()
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

static void updateSessions(StreamServerHandle* streamServer, std::function<int(ClientSession*)> onSessionUpdate)
{
	auto threadSystem = ThreadSystem::Instance::tryGet();
	streamServer->lockSessions();
	
	auto sessionCount = streamServer->getSessionCount();
	if (sessionCount == 0)
	{
		streamServer->unlockSessions();
		return;
	}

	auto sessions = streamServer->getSessions();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([streamServer, onSessionUpdate, sessions](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Server Session Update");

			auto currentTime = mpio::OS::getCurrentClock();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto streamSession = sessions[i];
				if (!streamSession.getSocket().getInstance())
					continue;
				auto reason = streamServer->updateSession(streamSession, currentTime);
				if (reason != SUCCESS_NETS_RESULT)
					streamServer->closeSession(streamSession, reason);
			}
			if (onSessionUpdate)
			{
				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					auto streamSession = sessions[i];
					if (!streamSession.getSocket().getInstance())
						continue;
					auto reason = onSessionUpdate((ClientSession*)streamSession.getHandle());
					if (reason != SUCCESS_NETS_RESULT)
						streamServer->closeSession(streamSession, reason);
				}
			}
		},
		sessionCount);
		threadPool.wait();
	}
	else
	{
		auto currentTime = mpio::OS::getCurrentClock();
		for (psize i = 0; i < sessionCount; i++)
		{
			auto streamSession = sessions[i];
			if (!streamSession.getSocket().getInstance())
				continue;
			auto reason = streamServer->updateSession(streamSession, currentTime);
			if (reason != SUCCESS_NETS_RESULT)
				streamServer->closeSession(streamSession, reason);
		}
		if (onSessionUpdate)
		{
			for (psize i = 0; i < sessionCount; i++)
			{
				auto streamSession = sessions[i];
				if (!streamSession.getSocket().getInstance())
					continue;
				auto reason = onSessionUpdate((ClientSession*)streamSession.getHandle());
				if (reason != SUCCESS_NETS_RESULT)
					streamServer->closeSession(streamSession, reason);
			}
		}
	}

	streamServer->unlockSessions();
}
void ServerNetworkSystem::update()
{
	SET_CPU_ZONE_SCOPED("Server Update");

	if (!streamServer)
		return;

	if (!streamServer->isRunning())
	{
		stop();
		return;
	}

	updateSessions(streamServer, onSessionUpdate);
}

void ServerNetworkSystem::start(SocketFamily socketFamily, const char* service, 
	size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, 
	size_t messageBufferSize, double timeoutTime, nets::SslContextView sslContext)
{
	GARDEN_ASSERT(service);
	GARDEN_ASSERT(sessionBufferSize > 0);
	GARDEN_ASSERT(connectionQueueSize > 0);
	GARDEN_ASSERT(receiveBufferSize > 0);
	GARDEN_ASSERT(messageBufferSize > 0);
	GARDEN_ASSERT(timeoutTime > 0);
	GARDEN_ASSERT(!streamServer);

	streamServer = new StreamServerHandle(this, socketFamily, service, sessionBufferSize, 
		connectionQueueSize, receiveBufferSize, messageBufferSize, timeoutTime, sslContext);

	if (!streamServer->isSecure())
		GARDEN_LOG_WARN("Server messages are not encrypted!");
	GARDEN_LOG_INFO("Started server.");
}
void ServerNetworkSystem::stop()
{
	if (!streamServer)
		return;

	if (streamServer->isRunning())
		GARDEN_LOG_INFO("Stopping server...");
	else
		GARDEN_LOG_WARN("Server is not running.");

	streamServer->destroy();
	delete streamServer;
	streamServer = nullptr;
}