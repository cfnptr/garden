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
	 * @details Client stops receive thread on this function false return result.
	 * @warning This function is called asynchronously from the receive thread!
	 *
	 * @param[in] session client session instance
	 * @param message received stream message
	 */
	using OnReceive = std::function<bool(StreamRequest)>;
private:
	tsl::robin_map<string, INetworkable*, SvHash, SvEqual> networkables;
	tsl::robin_map<string, OnReceive, SvHash, SvEqual> listeners;
	uint8* messageBuffer = nullptr;
	psize messageBufferSize = 0;
	psize messageByteCount = 0;
	uint8 messageLengthSize = 0;

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

	void onConnectionResult(NetsResult result) final;
	bool onStreamReceive(const uint8_t* receiveBuffer, size_t byteCount) final;
	static int onMessageReceive(::StreamMessage streamMessage, void* argument);

	friend class ecsm::Manager;
public:
	std::function<void(NetsResult)> onConnection = nullptr;

	/**
	 * @brief Returns stream message length size in bytes.
	 */
	uint8 getMessageLengthSize() const noexcept { return messageLengthSize; }
};

} // namespace garden