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

/***********************************************************************************************************************
 * @file
 * @brief Common network server functions.
 */

#pragma once
#include "garden/network.hpp"
#include "nets/stream-server.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;
class ServerNetworkSystem;

/**
 * @brief Network stream server handle.
 */
class StreamServerHandle final : public nets::IStreamServer
{
	tsl::robin_map<uint32, ClientSession*> datagramMap;
	ServerNetworkSystem* serverSystem = nullptr;
	void* cipher = nullptr;
	psize messageBufferSize = 0;
	uint8 messageLengthSize = 0;
public:
	using nets::IStreamServer::closeSession;

	StreamServerHandle(ServerNetworkSystem* serverSystem, SocketFamily socketFamily, const char* service, 
		size_t sessionBufferSize, size_t connectionQueueSize, size_t receiveBufferSize, 
		size_t messageBufferSize, double timeoutTime, nets::SslContextView sslContext);

	void closeSession(ClientSession* clientSession, int reason)
	{
		nets::IStreamServer::closeSession((StreamSession_T*)clientSession->streamSession, reason);
	}

	NetsResult sendDatagram(ClientSession* clientSession, const void* data, size_t byteCount);
	NetsResult sendDatagram(ClientSession* clientSession, const nets::OutStreamMessage& message)
	{
		GARDEN_ASSERT(message.isComplete());
		return sendDatagram(clientSession, message.getBuffer() + 
			messageLengthSize, message.getSize() - messageLengthSize);
	}
private:
	void* onSessionCreate(nets::StreamSessionView streamSession) final;
	void onSessionDestroy(nets::StreamSessionView streamSession, int reason) final;
	int onStreamReceive(nets::StreamSessionView streamSession, 
		const uint8_t* receiveBuffer, size_t byteCount) final;
	void onDatagramReceive(nets::SocketAddressView remoteAddress, 
		const uint8_t* receiveBuffer, size_t byteCount) final;
	static int onMessageReceive(::StreamMessage message, void* argument);

	int onEncRequest(ClientSession* session, StreamInput request);
	int onPingRequest(ClientSession* session, StreamInput request);

	friend class garden::ServerNetworkSystem;
};

/***********************************************************************************************************************
 * @brief Network server system.
 */
class ServerNetworkSystem final : public System, public Singleton<ServerNetworkSystem>
{
public:
	/**
	 * @brief On stream message receive from a client.
	 * @details Server destroys session on this function non zero return result.
	 * @warning This function is called asynchronously from the receive thread!
	 *
	 * @param[in] session client session instance
	 * @param message received stream message
	 */
	using OnReceive = std::function<int(ClientSession*, StreamInput)>;
private:
	tsl::robin_map<string, INetworkable*, SvHash, SvEqual> networkables;
	tsl::robin_map<string, OnReceive, SvHash, SvEqual> listeners;
	StreamServerHandle* streamServer = nullptr;

	/**
	 * @brief Creates a new network server system instance.
	 * @param setSingleton set system singleton instance
	 */
	ServerNetworkSystem(bool setSingleton = true);
	/**
	 * @brief Destroys network server system instance.
	 */
	~ServerNetworkSystem() final;

	void preInit();
	void update();

	friend class ecsm::Manager;
	friend class garden::StreamServerHandle;
public:
	std::function<int(nets::StreamSessionView, ClientSession*&)> onSessionCreate = nullptr;
	std::function<void(ClientSession*, int)> onSessionDestroy = nullptr;
	std::function<int(ClientSession*)> onSessionUpdate = nullptr;

	/**
	 * @brief Adds network message listener to the map.
	 * 
	 * @param messageType target message type string
	 * @param[in] onReceive on message receive function
	 */
	void addListener(string_view messageType, const OnReceive& onReceive)
	{
		GARDEN_ASSERT(!messageType.empty());
		GARDEN_ASSERT(onReceive);

		if (!listeners.emplace(messageType, onReceive).second)
			throw GardenError("Server message listener already registered.");
	}

	/**
	 * @brief Returns true if server receive thread is running.
	 */
	bool isRunning() const noexcept { return streamServer && streamServer->isRunning(); }
	/**
	 * @brief Returns true if server use encrypted connection.
	 */
	bool isSecure() const noexcept { return streamServer && streamServer->isSecure(); }

	/**
	 * @brief Starts server listening and receiving.
	 * 
	 * @param socketFamily local socket IP address family
	 * @param[in] service local IP address service string (port)
	 * @param sessionBufferSize maximum stream session count
	 * @param connectionQueueSize pending connections queue size
	 * @param receiveBufferSize receive data buffer size in bytes
	 * @param messageBufferSize biggest client request size in bytes
	 * @param timeoutTime session timeout time in seconds
	 * @param sslContext socket SSL context instance or NULL
	 *
	 * @throw Error with a @ref NetsResult string on failure.
	 */
	void start(SocketFamily socketFamily, const char* service, size_t sessionBufferSize = 512, 
		size_t connectionQueueSize = 256, size_t receiveBufferSize = UINT16_MAX + 2, size_t messageBufferSize = UINT8_MAX, 
		double timeoutTime = 5.0f, nets::SslContextView sslContext = nets::SslContextView(nullptr));
	/**
	 * @brief Stops server listening and receiving.
	 */
	void stop();
};

} // namespace garden