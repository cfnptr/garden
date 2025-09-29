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
 * @brief Common network functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "nets/stream-message.hpp"

namespace garden
{

/**
 * @brief Server client session data container.
 */
struct ClientSession
{
	void* streamSession = nullptr;
	uint8* messageBuffer = nullptr;
	psize messageByteCount = 0;
	bool isAuthorized = false;

	/**
	* @brief Sends stream data to the specified session.
	* @return The operation @ref NetsResult code.
	*
	* @param[in] sendBuffer data send buffer
	* @param byteCount data byte count to send
	*/
	NetsResult send(const void* sendBuffer, size_t byteCount) noexcept;
	/**
	* @brief Sends stream message to the specified session.
	* @return The operation @ref NetsResult code.
	* @param streamMessage stream message to send
	*/
	NetsResult send(nets::StreamMessage streamMessage) noexcept;
};

/**
 * @brief Base network system interface.
 */
class INetworkable
{
public:
	/**
	 * @brief Returns system message type string.
	 */
	virtual string_view getMessageType() = 0;

	/**
	 * @brief On message receive from a client.
	 * @details Server destroys session on this function non zero return result.
	 * @warning This function is called asynchronously from the receive thread!
	 *
	 * @param[in] session client session instance
	 * @param message received stream message
	 */
	virtual int onRequest(ClientSession* session, nets::StreamMessage message)
	{
		return NOT_SUPPORTED_NETS_RESULT;
	}
	/**
	 * @brief On message receive from the server.
	 * @details Client closes connection on this function non zero return result.
	 * @warning This function is called asynchronously from the receive thread!
	 * @param message received stream message
	 */
	virtual int onResponse(nets::StreamMessage message) { return NOT_SUPPORTED_NETS_RESULT; }
};

} // namespace garden