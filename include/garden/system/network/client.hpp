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
 * @brief Common network client functions.
 */

#pragma once
#include "garden/network.hpp"
#include "nets/stream-client.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief Network client system.
 */
class ClientNetworkSystem final : public System, public nets::IStreamClient, public Singleton<ClientNetworkSystem>
{
public:
	/**
	 * @brief On stream message receive from the server.
	 * @details Client stops receive thread on this function non zero return result.
	 * @warning This function is called asynchronously from the receive thread!
	 *
	 * @param[in] session client session instance
	 * @param message received stream message
	 */
	using OnReceive = std::function<int(StreamInput)>;
private:
	tsl::robin_map<string, INetworkable*, SvHash, SvEqual> networkables;
	tsl::robin_map<string, OnReceive, SvHash, SvEqual> listeners;
	vector<uint8> datagramBuffer;
	mutex datagramLocker;
	void* cipher = nullptr;
	uint8* encKey = nullptr;
	uint8* decKey = nullptr;
	void* encContext = nullptr;
	void* decContext = nullptr;
	uint8* messageBuffer = nullptr;
	psize messageBufferSize = 0;
	psize messageByteCount = 0;
	double pingMessageDelay = 0.0;
	float serverPing = 0.0f;
	uint32 datagramUID = 0;
	uint64 clientDatagramIdx = 1;
	uint64 serverDatagramIdx = 0;
	int lastDisconnectReason = 0;
	uint8 messageLengthSize = 0;
	bool isDatagram = false;
	uint16 _alignment = 0;

	/**
	 * @brief Creates a new network client system instance.
	 * @param receiveBufferSize receive data buffer size in bytes
	 * @param messageBufferSize biggest server response size in bytes
	 * @param timeoutTime server timeout time in seconds
	 * @param setSingleton set system singleton instance
	 */
	ClientNetworkSystem(size_t receiveBufferSize = UINT16_MAX + 2, size_t messageBufferSize = UINT16_MAX, 
		double timeoutTime = 5.0f, bool setSingleton = true);
	/**
	 * @brief Destroys network client system instance.
	 */
	~ClientNetworkSystem() final;

	void preInit();
	void preDeinit();
	void update();

	void onConnectionResult(NetsResult result) final;
	void onDisconnect(int reason) final;
	int onStreamReceive(const uint8_t* receiveBuffer, size_t byteCount) final;
	int onDatagramReceive(const uint8_t* receiveBuffer, size_t byteCount) final;
	static int onMessageReceive(::StreamMessage message, void* argument);

	int onEncResponse(StreamInput response);
	friend class ecsm::Manager;
public:
	/**
	 * @brief Stream client connection result function. (TCP)
	 * @warning This function is called asynchronously from the receive thread!
	 */
	std::function<void(NetsResult)> onConnection = nullptr;
	/**
	 * @brief Is network client authorized on the server.
	 */
	volatile bool isAuthorized = false;

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
			throw GardenError("Client message listener already registered.");
	}

	/**
	 * @brief Returns stream message length size in bytes.
	 */
	uint8 getMessageLengthSize() const noexcept { return messageLengthSize; }
	/**
	 * @brief Returns last network client disconnection reason.
	 */
	int getLastDisconnectReason() const noexcept { return lastDisconnectReason; }
	/**
	 * @brief Returns game server ping time in seconds.
	 */
	float getPing() const noexcept { return serverPing; }

	/**
	 * @brief Sends datagram to the server. (UDP)
	 * @return The operation @ref NetsResult code.
	 *
	 * @param[in] data send data buffer
	 * @param byteCount data byte count to send
	 */
	NetsResult sendDatagram(const void* data, size_t byteCount) noexcept;
	/**
	 * @brief Sends datagram message to the server. (UDP)
	 * @return The operation @ref NetsResult code.
	 * @param[in] message datagram message to send
	 */
	NetsResult sendDatagram(const StreamOutput& message) noexcept
	{
		GARDEN_ASSERT(message.isComplete());
		return sendDatagram(message.getBuffer() + 
			messageLengthSize, message.getSize() - messageLengthSize);
	}

	/**
	 * @brief Disconnects networn client from the server.
	 * @param reason client disconnection reason
	 */
	void disconnect(int reason) noexcept;
};

} // namespace garden