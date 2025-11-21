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
#include "openssl/rand.h"

using namespace garden;

StreamServerHandle::StreamServerHandle(ServerNetworkSystem* serverSystem, SocketFamily socketFamily, const char* service, 
	size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, size_t messageBufferSize, 
	uint8_t serverLengthSize, double timeoutTime, nets::SslContextView sslContext)
	:
	nets::IStreamServer(socketFamily, service, sessionBufferSize, 
		connectionQueueSize, receiveBufferSize, timeoutTime, sslContext),
	serverSystem(serverSystem), serverLengthSize(serverLengthSize)
{
	if (messageBufferSize <= UINT8_MAX) clientLengthSize = sizeof(uint8);
	else if (messageBufferSize <= UINT16_MAX) clientLengthSize = sizeof(uint16);
	else if (messageBufferSize <= UINT32_MAX) clientLengthSize = sizeof(uint32);
	else if (messageBufferSize <= UINT64_MAX) clientLengthSize = sizeof(uint64);
	else abort();

	this->messageBufferSize = messageBufferSize + clientLengthSize;
	GARDEN_ASSERT(this->messageBufferSize <= receiveBufferSize);
}

static NetsResult sendEncMessage(ClientSession* clientSession, uint8 messageLengthSize)
{
	constexpr uint8 messageSize = sizeof(uint32) + ClientSession::keySize, bufferSize = messageSize + 16;
	StreamOutputBuffer<bufferSize> message("enc", messageSize, messageLengthSize);
	message.write((const void*)&clientSession->datagramUID, sizeof(uint32)); // Note: No endianness swap for random data.
	message.write(clientSession->encKey, ClientSession::keySize);
	auto result = clientSession->send(message);
	OPENSSL_cleanse(message.buffer, bufferSize * sizeof(uint8));
	return result;
}
NetsResult StreamServerHandle::sendDatagram(ClientSession* clientSession, const void* data, size_t byteCount)
{
	clientSession->datagramLocker.lock();

	psize totalSize;
	if (isSecure())
	{
		if (clientSession->serverDatagramIdx % (uint64)UINT32_MAX == 0) // Note: rekeying
		{
			if (!RAND_bytes(clientSession->encKey, ClientSession::keySize) ||
				!ClientSession::updateEncDecKey(clientSession->encContext, clientSession->encKey))
			{
				return FAILED_TO_CREATE_SSL_NETS_RESULT;
			}
			auto result = sendEncMessage(clientSession, serverLengthSize);
			if (result != SUCCESS_NETS_RESULT)
				return result;
		}

		totalSize = clientSession->encryptDatagram(data, byteCount);
		if (totalSize == 0)
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
	}
	else
	{
		totalSize = ClientSession::packDatagram(data, byteCount, clientSession->datagramBuffer, 
			clientSession->datagramUID, clientSession->serverDatagramIdx);
	}

	auto result = nets::IStreamServer::sendDatagram((SocketAddress_T*)
		clientSession->datagramAddress, clientSession->datagramBuffer.data(), totalSize);
	clientSession->datagramLocker.unlock();
	return result;
}

void* StreamServerHandle::onSessionCreate(nets::StreamSessionView streamSession)
{
	SET_CPU_ZONE_SCOPED("On Server Session Create");

	streamSession.getSocket().setNoDelay(true);
	uint8* encKey = nullptr; void* encContext = nullptr;

	if (isSecure())
	{
		encContext = ClientSession::createEncContext(encKey, cipher);
		if (!encContext)
		{
			GARDEN_LOG_ERROR("Failed to generate session encryption context.");
			return NULL;
		}
	}

	ClientSession* clientSession;
	if (serverSystem->onSessionCreate)
	{
		clientSession = nullptr;
		auto result = serverSystem->onSessionCreate(streamSession, clientSession);
		if (result != SUCCESS_NETS_RESULT)
		{
			ClientSession::destroyEncDecContext(encContext, encKey);
			GARDEN_LOG_INFO("Rejected client session. (address: " + 
				streamSession.getAddress() + ", reason: " + reasonToString(result) + ")");
			return NULL;
		}
	}
	else
	{
		clientSession = new ClientSession();
		clientSession->isAuthorized = true;
	}

	clientSession->streamSession = streamSession.getInstance();
	clientSession->messageBuffer = new uint8[messageBufferSize];
	clientSession->encContext = encContext;
	clientSession->encKey = encKey;

	while (isRunning()) // TODO: maybe add time out?
	{
		uint32 datagramUID = 0;
		if (!RAND_bytes((uint8*)&datagramUID, sizeof(uint32)))
			continue;
		auto result = datagramMap.emplace(datagramUID, clientSession);
		if (!result.second)
			continue;
		clientSession->datagramUID = datagramUID;
		break;
	}

	if (isSecure())
	{
		if (sendEncMessage(clientSession, serverLengthSize) != SUCCESS_NETS_RESULT)
			streamSession.shutdown();
	}
	else
	{
		StreamOutputBuffer<16 + sizeof(uint32)> message("enc", sizeof(uint32), serverLengthSize);
		message.write((const void*)&clientSession->datagramUID, sizeof(uint32)); // Note: No endianness swap for random data.
		if (clientSession->send(message) != SUCCESS_NETS_RESULT)
		{
			streamSession.shutdown();
			GARDEN_LOG_INFO("Failed to send session enc. (address: " + streamSession.getAddress() + ")");
		}
	}

	GARDEN_LOG_INFO("Created a new client session. (address: " + streamSession.getAddress() + ")");
	return clientSession;
}
void StreamServerHandle::onSessionDestroy(nets::StreamSessionView streamSession, int reason)
{
	SET_CPU_ZONE_SCOPED("On Server Session Destroy");

	auto clientSession = (ClientSession*)streamSession.getHandle();
	delete[] clientSession->messageBuffer; clientSession->messageBuffer = nullptr;

	auto result = datagramMap.erase(clientSession->datagramUID);
	GARDEN_ASSERT_MSG(result == 1, "Detected memory corruption");

	if (isSecure())
	{
		clientSession->datagramLocker.lock();
		ClientSession::destroyEncDecContext(clientSession->encContext, clientSession->encKey);
		ClientSession::destroyEncDecContext(clientSession->decContext, clientSession->decKey);
		clientSession->encContext = clientSession->decContext = nullptr;
		clientSession->encKey = clientSession->decKey = nullptr; 
		clientSession->datagramLocker.unlock();
	}
	clientSession->datagramUID = 0;

	if (serverSystem->onSessionDestroy)
		serverSystem->onSessionDestroy(clientSession, reason);
	else
	 	delete clientSession;

	GARDEN_LOG_INFO("Destroyed client session. (address: " + 
		streamSession.getAddress() + ", reason: " + reasonToString(reason) + ")");
}

//**********************************************************************************************************************
int StreamServerHandle::onStreamReceive(nets::StreamSessionView streamSession, 
	const uint8_t* receiveBuffer, size_t byteCount)
{
	auto clientSession = (ClientSession*)streamSession.getHandle();
	std::pair<ServerNetworkSystem*, ClientSession*> pair = { serverSystem, clientSession };
	return handleStreamMessage(receiveBuffer, byteCount, clientSession->messageBuffer, messageBufferSize, 
		&clientSession->messageByteCount, clientLengthSize, onMessageReceive, &pair);
}
void StreamServerHandle::onDatagramReceive(nets::SocketAddressView remoteAddress, 
	const uint8_t* receiveBuffer, size_t byteCount)
{
	SET_CPU_ZONE_SCOPED("On Datagram Receive");

	if (byteCount <= ClientSession::ivSize)
		return;

	auto searchResult = datagramMap.find(*((const uint32*)receiveBuffer));
	if (searchResult == datagramMap.end())
		return;

	auto clientSession = searchResult->second;
	auto datagramIndex = leToHost64(*((const uint64*)(receiveBuffer + sizeof(uint32))));
	if (datagramIndex <= clientSession->clientDatagramIdx)
		return;

	auto sessionAddress = nets::SocketAddressView(getStreamSessionRemoteAddress(
		(StreamSession_T*)clientSession->streamSession));
	if (sessionAddress.getIpSize() != remoteAddress.getIpSize() || memcmp(
		sessionAddress.getIP(), remoteAddress.getIP(), sessionAddress.getIpSize()) != 0)
	{
		return;
	}

	::StreamMessage message;
	if (isSecure())
	{
		clientSession->datagramLocker.lock();
		if (!clientSession->decContext)
		{
			clientSession->datagramLocker.unlock();
			return;
		}

		auto dataSize = clientSession->decryptDatagram(receiveBuffer, byteCount);
		if (dataSize == 0 || dataSize >= byteCount)
		{
			clientSession->datagramLocker.unlock();
			return;
		}

		memcpy((void*)receiveBuffer, clientSession->datagramBuffer.data(), dataSize);
		clientSession->datagramLocker.unlock();

		message.iter = (uint8*)receiveBuffer;
		message.end = message.iter + dataSize;
	}
	else
	{
		message.iter = (uint8*)receiveBuffer + ClientSession::ivSize;
		message.end = message.iter + (byteCount - ClientSession::ivSize);
	}

	clientSession->clientDatagramIdx = datagramIndex;
	clientSession->datagramAddress = remoteAddress.getInstance();
	std::pair<ServerNetworkSystem*, ClientSession*> pair = { serverSystem, clientSession };

	auto result = onMessageReceive(message, &pair);
	clientSession->datagramAddress = nullptr;

	if (result != SUCCESS_NETS_RESULT)
	{
		destroySession(clientSession, result);
		return;
	}
	clientSession->alive();
}

//**********************************************************************************************************************
int StreamServerHandle::onMessageReceive(::StreamMessage message, void* argument)
{
	SET_CPU_ZONE_SCOPED("On Message Receive");

	if (getStreamMessageLeft(message) < sizeof(uint8) * 3)
		return BAD_DATA_NETS_RESULT;

	bool isSystem;
	if (readStreamMessageBool(&message, &isSystem))
		return BAD_DATA_NETS_RESULT;

	const void* typeString; psize typeLength;
	if (readStreamMessageData(&message, &typeString, &typeLength, sizeof(uint8)))
		return BAD_DATA_NETS_RESULT;

	string_view messageType((const char*)typeString, typeLength);
	auto pair = *((std::pair<ServerNetworkSystem*, ClientSession*>*)argument);

	if (isSystem)
	{
		auto searchResult = pair.first->networkables.find(messageType);
		if (searchResult != pair.first->networkables.end())
			return searchResult->second->onRequest(pair.second, message);
	}
	else
	{
		auto searchResult = pair.first->listeners.find(messageType);
		if (searchResult != pair.first->listeners.end())
			return searchResult->second(pair.second, message);
	}
	return BAD_DATA_NETS_RESULT;
}

int StreamServerHandle::onEncRequest(ClientSession* session, StreamInput request)
{
	if (session->datagramAddress || !isSecure())
		return BAD_DATA_NETS_RESULT;

	const void* newKey;
	if (request.read(newKey, ClientSession::keySize))
		return BAD_DATA_NETS_RESULT;

	session->datagramLocker.lock();

	if (!session->decKey)
		session->decKey = new uint8[ClientSession::keySize];
	memcpy(session->decKey, newKey, ClientSession::keySize * sizeof(uint8));

	if (session->decContext)
	{
		if (!ClientSession::updateEncDecKey(session->decContext, session->decKey))
		{
			session->datagramLocker.unlock();
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
		}
	}
	else
	{
		auto decContext = ClientSession::createDecContext(session->decKey, cipher);
		if (!decContext)
		{
			session->datagramLocker.unlock();
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
		}
		session->decContext = decContext;
	}

	session->datagramLocker.unlock();
	return request.isComplete() ? SUCCESS_NETS_RESULT : BAD_DATA_NETS_RESULT;
}
int StreamServerHandle::onPingRequest(ClientSession* session, StreamInput request)
{
	if (!session->isAuthorized || !session->datagramAddress || !request.isComplete())
		return BAD_DATA_NETS_RESULT;
	StreamOutputBuffer<16> response("pong", 0, serverLengthSize);
	return sendDatagram(session, response);
}

//**********************************************************************************************************************
ServerNetworkSystem::ServerNetworkSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ServerNetworkSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ServerNetworkSystem::update);
}
ServerNetworkSystem::~ServerNetworkSystem()
{
	stop();

	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ServerNetworkSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ServerNetworkSystem::update);
	}
	unsetSingleton();
}

void ServerNetworkSystem::preInit()
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

	addListener("enc", [this](ClientSession* session, StreamInput request)
	{
		return streamServer->onEncRequest(session, request);
	});
	addListener("ping", [this](ClientSession* session, StreamInput request)
	{
		return streamServer->onPingRequest(session, request);
	});
}

//**********************************************************************************************************************
static void updateSessions(StreamServerHandle* streamServer, std::function<int(ClientSession*)> onSessionUpdate)
{
	auto manager = Manager::Instance::get();
	manager->unlock();

	auto threadSystem = ThreadSystem::Instance::tryGet();
	streamServer->lockSessions();
	
	auto sessions = streamServer->getSessions();
	auto sessionCount = streamServer->getSessionCount();
	if (sessionCount == 0)
	{
		streamServer->unlockSessions();
		return;
	}
	
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
				auto result = streamServer->updateSession(streamSession, currentTime);
				if (result != SUCCESS_NETS_RESULT)
					streamServer->destroySession(streamSession, result);
			}
			if (onSessionUpdate)
			{
				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					auto streamSession = sessions[i];
					if (!streamSession.getSocket().getInstance())
						continue;
					auto result = onSessionUpdate((ClientSession*)streamSession.getHandle());
					if (result != SUCCESS_NETS_RESULT)
						streamServer->destroySession(streamSession, result);
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
			auto result = streamServer->updateSession(streamSession, currentTime);
			if (result != SUCCESS_NETS_RESULT)
				streamServer->destroySession(streamSession, result);
		}
		if (onSessionUpdate)
		{
			for (psize i = 0; i < sessionCount; i++)
			{
				auto streamSession = sessions[i];
				if (!streamSession.getSocket().getInstance())
					continue;
				auto result = onSessionUpdate((ClientSession*)streamSession.getHandle());
				if (result != SUCCESS_NETS_RESULT)
					streamServer->destroySession(streamSession, result);
			}
		}
	}

	streamServer->flushSessions();
	streamServer->unlockSessions();
	manager->lock();
}
void ServerNetworkSystem::update()
{
	SET_CPU_ZONE_SCOPED("Server Update");

	if (!streamServer)
		return;

	if (!streamServer->isRunning())
	{
		Manager::Instance::get()->isRunning = false;
		stop();
		return;
	}

	updateSessions(streamServer, onSessionUpdate);
}

//**********************************************************************************************************************
void ServerNetworkSystem::start(SocketFamily socketFamily, const char* service, psize sessionBufferSize, 
	psize connectionQueueSize, psize receiveBufferSize, psize messageBufferSize, 
	uint8 serverLengthSize, double timeoutTime, nets::SslContextView sslContext)
{
	GARDEN_ASSERT(service);
	GARDEN_ASSERT(sessionBufferSize > 0);
	GARDEN_ASSERT(connectionQueueSize > 0);
	GARDEN_ASSERT(receiveBufferSize > 0);
	GARDEN_ASSERT(messageBufferSize > 0);
	GARDEN_ASSERT(timeoutTime > 0);
	GARDEN_ASSERT(!streamServer);

	streamServer = new StreamServerHandle(this, socketFamily, service, sessionBufferSize, connectionQueueSize, 
		receiveBufferSize, messageBufferSize, serverLengthSize, timeoutTime, sslContext);

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

	auto manager = Manager::Instance::get();
	manager->unlock();
	streamServer->destroy();
	manager->lock();

	ClientSession::destroyCipher(streamServer->cipher);
	delete streamServer; streamServer = nullptr;
}