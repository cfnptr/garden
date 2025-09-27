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
#include "nets/defines.h"

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
};

/**
 * @brief Network message container.
 */
struct NetworkMessage : public StreamMessage
{
	/**
	 * @brief Reads data from the network message.
	 * @return True if no more data to read, otherwise false.
	 *
	 * @param[out] data pointer to the message data
	 * @param size message byte count to read
	 */
	bool read(const uint8_t*& data, size_t count)
	{
		if (offset + count > size)
			return true;
		data = buffer + offset; offset += size;
		return false;
	}
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
	 * @brief On message from the client receive.
	 * @details Server destroys session on this function false return result.
	 * @warning This function is called asynchronously from the receive thread!
	 *
	 * @param[in] session client session instance
	 * @param message received stream message
	 */
	virtual NetsResult onRequest(ClientSession* session, NetworkMessage message) { return NOT_SUPPORTED_NETS_RESULT; }
	/**
	 * @brief On message from the server receive.
	 * @details Client closes connection on this function false return result.
	 * @warning This function is called asynchronously from the receive thread!
	 * @param message received stream message
	 */
	virtual NetsResult onResponse(NetworkMessage message) { return NOT_SUPPORTED_NETS_RESULT; }
};

} // namespace garden