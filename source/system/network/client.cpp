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
#include "openssl/rand.h"

using namespace garden;

static NetsResult sendEncMessage(nets::IStreamClient* streamClient, uint8* encKey, uint8 messageLengthSize)
{
	constexpr uint8 bufferSize = ClientSession::keySize + 16;
	StreamOutputBuffer<bufferSize> message("enc", ClientSession::keySize, messageLengthSize);
	message.write(encKey, ClientSession::keySize);
	auto result = streamClient->send(message);
	OPENSSL_cleanse(message.buffer, bufferSize * sizeof(uint8));
	return result;
}

void ClientNetworkSystem::onConnectionResult(NetsResult result)
{
	if (result == SUCCESS_NETS_RESULT && isSecure())
	{
		datagramLocker.lock();
		if (encContext)
		{
			if (!RAND_bytes(encKey, ClientSession::keySize) ||
				!ClientSession::updateEncDecKey(encContext, encKey))
			{
				result = FAILED_TO_CREATE_SSL_NETS_RESULT;
			}
		}
		else
		{
			encContext = ClientSession::createEncContext(encKey, cipher);
			if (!encContext)
				result = FAILED_TO_CREATE_SSL_NETS_RESULT;
		}
		datagramLocker.unlock();

		if (result == SUCCESS_NETS_RESULT)
			result = sendEncMessage(this, encKey, clientLengthSize);
	}

	if (result == SUCCESS_NETS_RESULT) GARDEN_LOG_INFO("Connected to the server.");
	else GARDEN_LOG_WARN("Failed to connect to the server. (reason: " + string(netsResultToString(result)) + ")");

	if (onConnection)
		onConnection(result);
	else isAuthorized = true;
}
void ClientNetworkSystem::onDisconnect(int reason)
{
	GARDEN_LOG_INFO("Disconnected from the server. (reason: " + reasonToString(reason) + ")");

	datagramLocker.lock();
	if (isSecure())
	{
		OPENSSL_cleanse(encKey, ClientSession::keySize * sizeof(uint8));
		if (decKey)
			OPENSSL_cleanse(decKey, ClientSession::keySize * sizeof(uint8));
	}
	clientDatagramIdx = 1; serverDatagramIdx = 0; datagramUID = 0;
	datagramLocker.unlock();

	auto manager = Manager::Instance::get();
	manager->lock();
	pingMessageDelay = 0.0; serverPing = 0.0f;
	lastDisconnectReason = reason;
	manager->unlock();
	isAuthorized = false;
}

//**********************************************************************************************************************
int ClientNetworkSystem::onStreamReceive(const uint8_t* receiveBuffer, size_t byteCount)
{
	return handleStreamMessage(receiveBuffer, byteCount, messageBuffer, 
		messageBufferSize, &messageByteCount, serverLengthSize, onMessageReceive, this);
}
int ClientNetworkSystem::onDatagramReceive(const uint8_t* receiveBuffer, size_t byteCount)
{
	SET_CPU_ZONE_SCOPED("On Datagram Receive");

	if (byteCount < ClientSession::ivSize)
		return BAD_DATA_NETS_RESULT;

	auto datagramIndex = leToHost64(*((const uint64*)(receiveBuffer + sizeof(uint32))));
	if (datagramUID != *((const uint32*)receiveBuffer) || datagramIndex <= serverDatagramIdx)
		return SUCCESS_NETS_RESULT;

	::StreamMessage message;
	if (isSecure())
	{
		datagramLocker.lock();
		if (!decContext)
		{
			datagramLocker.unlock();
			return SUCCESS_NETS_RESULT;
		}

		auto dataSize = ClientSession::decryptDatagram(receiveBuffer, byteCount, decContext, datagramBuffer);
		if (dataSize == 0 || dataSize >= byteCount)
		{
			datagramLocker.unlock();
			return BAD_DATA_NETS_RESULT;
		}

		memcpy((void*)receiveBuffer, datagramBuffer.data(), dataSize);
		datagramLocker.unlock();

		message.iter = (uint8*)receiveBuffer;
		message.end = message.iter + dataSize;
	}
	else
	{
		message.iter = (uint8*)receiveBuffer + ClientSession::ivSize;
		message.end = message.iter + (byteCount - ClientSession::ivSize);
	}

	serverDatagramIdx = datagramIndex;
	isDatagram = true;

	auto result = onMessageReceive(message, this);
	isDatagram = false;

	if (result != SUCCESS_NETS_RESULT)
		return result;

	alive();
	return result;
}

//**********************************************************************************************************************
int ClientNetworkSystem::onMessageReceive(::StreamMessage message, void* argument)
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
	auto clientSystem = (ClientNetworkSystem*)argument;

	if (isSystem)
	{
		auto searchResult = clientSystem->networkables.find(messageType);
		if (searchResult != clientSystem->networkables.end())
			return searchResult->second->onMsgFromServer(message, clientSystem->isDatagram);
	}
	else
	{
		auto searchResult = clientSystem->listeners.find(messageType);
		if (searchResult != clientSystem->listeners.end())
			return searchResult->second(message, clientSystem->isDatagram);
	}
	return BAD_DATA_NETS_RESULT;
}

int ClientNetworkSystem::onEncResponse(StreamInput response, bool isDatagram)
{
	if (isDatagram)
		return BAD_DATA_NETS_RESULT;

	const void* datagramUID;
	if (response.read(datagramUID, sizeof(uint32)))
		return BAD_DATA_NETS_RESULT;

	// Note: No endianness swap for random data.
	this->datagramUID = *((const uint32*)datagramUID);

	if (!isSecure())
	{
		if (!response.isComplete())
			return BAD_DATA_NETS_RESULT;
		return SUCCESS_NETS_RESULT;
	}

	const void* newKey;
	if (response.read(newKey, ClientSession::keySize))
		return BAD_DATA_NETS_RESULT;

	datagramLocker.lock();

	if (!decKey)
		decKey = new uint8[ClientSession::keySize];
	memcpy(decKey, newKey, ClientSession::keySize * sizeof(uint8));

	if (decContext)
	{
		if (!ClientSession::updateEncDecKey(decContext, decKey))
		{
			datagramLocker.unlock();
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
		}
	}
	else
	{
		auto decContext = ClientSession::createDecContext(decKey, cipher);
		if (!decContext)
		{
			datagramLocker.unlock();
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
		}
		decContext = decContext;
	}

	GARDEN_LOG_INFO("Secured datagram connection.");
	datagramLocker.unlock();

	return response.isComplete() ? SUCCESS_NETS_RESULT : BAD_DATA_NETS_RESULT;
}

//**********************************************************************************************************************
ClientNetworkSystem::ClientNetworkSystem(psize receiveBufferSize, psize messageBufferSize, 
	uint8 clientLengthSize, double timeoutTime, bool setSingleton)
	: 
	Singleton(setSingleton), nets::IStreamClient(receiveBufferSize, timeoutTime), clientLengthSize(clientLengthSize)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ClientNetworkSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", ClientNetworkSystem::preDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", ClientNetworkSystem::update);

	if (messageBufferSize <= UINT8_MAX) serverLengthSize = sizeof(uint8);
	else if (messageBufferSize <= UINT16_MAX) serverLengthSize = sizeof(uint16);
	else if (messageBufferSize <= UINT32_MAX) serverLengthSize = sizeof(uint32);
	else if (messageBufferSize <= UINT64_MAX) serverLengthSize = sizeof(uint64);
	else abort();

	this->messageBufferSize = messageBufferSize + serverLengthSize;
	GARDEN_ASSERT(this->messageBufferSize <= receiveBufferSize);
	this->messageBuffer = new uint8[this->messageBufferSize];
}
ClientNetworkSystem::~ClientNetworkSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ClientNetworkSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", ClientNetworkSystem::preDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", ClientNetworkSystem::update);

		ClientSession::destroyEncDecContext(encContext, encKey);
		ClientSession::destroyEncDecContext(decContext, decKey);
		ClientSession::destroyCipher(cipher);
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

	addListener("ping", [this](StreamInput request, bool isDatagram)
	{
		if (!isDatagram || !request.isComplete())
			return (NetsResult)BAD_DATA_NETS_RESULT;
		StreamOutputBuffer<16> message("pong", 0, clientLengthSize);
		return sendDatagram(message);
	});
	addListener("pong", [this](StreamInput response, bool isDatagram)
	{
		if (!isDatagram || !response.isComplete())
			return BAD_DATA_NETS_RESULT;

		auto currentTime = mpio::OS::getCurrentClock();
		auto manager = Manager::Instance::get();
		manager->lock();
		serverPing = currentTime - (pingMessageDelay - 1.0);
		manager->unlock();

		// GARDEN_LOG_DEBUG("Server ping: " + to_string(serverPing * 1000.0) + "ms.");
		return SUCCESS_NETS_RESULT;
	});
	addListener("enc", [this](StreamInput message, bool isDatagram)
	{
		return onEncResponse(message, isDatagram);
	});
}
void ClientNetworkSystem::preDeinit()
{
	destroy();
}

//**********************************************************************************************************************
void ClientNetworkSystem::update()
{
	SET_CPU_ZONE_SCOPED("Client Update");
	nets::IStreamClient::update();

	if (isConnected() && isAuthorized)
	{
		auto currentTime = mpio::OS::getCurrentClock();
		if (currentTime > pingMessageDelay)
		{
			StreamOutputBuffer<16> message("ping", 0, clientLengthSize);
			auto result = sendDatagram(message);
			if (result != SUCCESS_NETS_RESULT)
				disconnect(result);
			pingMessageDelay = currentTime + 1.0;
		}
	}
}

NetsResult ClientNetworkSystem::sendDatagram(const void* data, size_t byteCount) noexcept
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(byteCount > 0);
	datagramLocker.lock();

	psize totalSize;
	if (isSecure())
	{
		if (clientDatagramIdx % (uint64)UINT32_MAX == 0 && isSecure()) // Note: rekeying
		{
			if (!RAND_bytes(encKey, ClientSession::keySize) || !ClientSession::updateEncDecKey(encContext, encKey))
				return FAILED_TO_CREATE_SSL_NETS_RESULT;
			auto result = sendEncMessage(this, encKey, clientLengthSize);
			if (result != SUCCESS_NETS_RESULT)
				return result;
		}

		totalSize = ClientSession::encryptDatagram(data, byteCount, 
			encContext, datagramBuffer, datagramUID, clientDatagramIdx);
		if (totalSize == 0)
			return FAILED_TO_CREATE_SSL_NETS_RESULT;
	}
	else
	{
		totalSize = ClientSession::packDatagram(data, byteCount, datagramBuffer, datagramUID, clientDatagramIdx);
	}
	
	auto result = nets::IStreamClient::sendDatagram(datagramBuffer.data(), totalSize);
	datagramLocker.unlock();
	return result;
}

void ClientNetworkSystem::disconnect(int reason) noexcept
{
	if (isRunning())
		GARDEN_LOG_INFO("Disconnected network client. (reason: " + reasonToString(reason) + ")");
	nets::IStreamClient::disconnect();
}