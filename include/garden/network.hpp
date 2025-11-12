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
#include "math/vector.hpp"
#include <mutex>

namespace garden
{

/**
 * @brief Network stream input data container.
 */
class StreamInput : public nets::StreamMessage
{
public:
	using nets::StreamMessage::read;

	/**
	 * @brief Creates a new stream input container.
	 * @param streamMessage target stream message data
	 */
	StreamInput(::StreamMessage streamMessage) noexcept
	{
		iter = streamMessage.iter;
		end = streamMessage.end;
	}
	/**
	 * @brief Creates a new stream input container.
	 * @param streamMessage target stream message data
	 */
	StreamInput(nets::StreamMessage streamMessage) noexcept { *this = streamMessage; }
	/**
	 * @brief Creates a new empty stream input container.
	 */
	StreamInput() noexcept = default;

	/**
	 * @brief Reads 32-bit uint 2 component vector from the stream message and advances offset.
	 * @return True if no more data to read, otherwise false.
	 * @param[out] value pointer to the unsigned integer vector
	 */
	bool read(uint2& value) noexcept
	{
		if (iter + sizeof(uint2) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		value = *((const uint2*)iter);
		#else
		auto data = (const uint2*)iter;
		value = uint2(leToHost32(data->x), leToHost32(data->y));
		#endif
		iter += sizeof(uint2);
		return false;
	}
	/**
	 * @brief Reads 32-bit uint 3 component vector from the stream message and advances offset.
	 * @return True if no more data to read, otherwise false.
	 * @param[out] value pointer to the unsigned integer vector
	 */
	bool read(uint3& value) noexcept
	{
		if (iter + sizeof(uint3) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		value = *((const uint3*)iter);
		#else
		auto data = (const uint3*)iter;
		value = uint3(leToHost32(data->x), leToHost32(data->y), leToHost32(data->z));
		#endif
		iter += sizeof(uint3);
		return false;
	}
	/**
	 * @brief Reads 32-bit uint 4 component vector from the stream message and advances offset.
	 * @return True if no more data to read, otherwise false.
	 * @param[out] value pointer to the unsigned integer vector
	 */
	bool read(uint4& value) noexcept
	{
		if (iter + sizeof(uint4) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		value = *((const uint4*)iter);
		#else
		auto data = (const uint4*)iter;
		value = uint4(leToHost32(data->x), leToHost32(data->y), leToHost32(data->z), leToHost32(data->w));
		#endif
		iter += sizeof(uint4);
		return false;
	}
};

/***********************************************************************************************************************
 * @brief Network stream output data container.
 */
class StreamOutput : public nets::OutStreamMessage
{
public:
	using nets::OutStreamMessage::write;
protected:
	inline static psize fullSize(string_view type, psize messageSize) noexcept
	{
		return messageSize + type.size() + sizeof(uint8) * 2;
	}
	inline void writeHeader(string_view type, bool isSystem) noexcept
	{
		GARDEN_ASSERT(!type.empty());
		GARDEN_ASSERT(type.size() <= UINT8_MAX);
		auto result = write(isSystem);
		result |= write(type, sizeof(uint8));
		GARDEN_ASSERT_MSG(!result, "Stream message buffer is too small");
	}
public:
	static constexpr uint8 baseTotalSize = maxLengthSize + 3; /**< 3 = type + typeSize + isSystem */

	/**
	 * @brief Creates a new stream output container.
	 *
	 * @param type message type string
	 * @param[in,out] buffer message data buffer
	 * @param bufferSize message buffer size in bytes
	 * @param messageSize message size in bytes
	 * @param lengthSize message header length size in bytes
	 * @param isSystem is this system message
	 */
	StreamOutput(string_view type, uint8* buffer, psize bufferSize, 
		psize messageSize, uint8 lengthSize, bool isSystem = false) noexcept :
		nets::OutStreamMessage(buffer, bufferSize, fullSize(type, messageSize), lengthSize)
	{ writeHeader(type, isSystem); }
	/**
	 * @brief Creates a new stream output container.
	 *
	 * @param type message type string
	 * @param[in,out] buffer message data buffer
	 * @param messageSize message size in bytes
	 * @param lengthSize message header length size in bytes
	 * @param isSystem is this system message
	 */
	StreamOutput(string_view type, vector<uint8>& buffer,
		psize messageSize, uint8 lengthSize, bool isSystem = false) noexcept :
		nets::OutStreamMessage(buffer, fullSize(type, messageSize), lengthSize)
	{ writeHeader(type, isSystem); }
	/**
	 * @brief Creates a new empty stream output container.
	 */
	StreamOutput() noexcept = default;

	/**
	 * @brief Writes 32-bit uint 2 component vector to the stream message and advances offset.
	 * @return True if no more space to write data, otherwise false.
	 * @param value unsigned integer vector to write
	 */
	bool write(uint2 value) noexcept
	{
		if (iter + sizeof(uint2) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		*((uint2*)iter) = value;
		#else
		*((uint2*)iter) = uint2(leToHost32(value.x), leToHost32(value.y));
		#endif
		iter += sizeof(uint2);
		return false;
	}
	/**
	 * @brief Writes 32-bit uint 3 component vector to the stream message and advances offset.
	 * @return True if no more space to write data, otherwise false.
	 * @param value unsigned integer vector to write
	 */
	bool write(uint3 value) noexcept
	{
		if (iter + sizeof(uint3) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		*((uint3*)iter) = value;
		#else
		*((uint3*)iter) = uint3(leToHost32(value.x), leToHost32(value.y), leToHost32(value.z));
		#endif
		iter += sizeof(uint3);
		return false;
	}
	/**
	 * @brief Writes 32-bit uint 4 component vector to the stream message and advances offset.
	 * @return True if no more space to write data, otherwise false.
	 * @param value unsigned integer vector to write
	 */
	bool write(uint4 value) noexcept
	{
		if (iter + sizeof(uint4) > end)
			return true;
		#if NETS_LITTLE_ENDIAN
		*((uint4*)iter) = value;
		#else
		*((uint4*)iter) = uint4(leToHost32(value.x), leToHost32(value.y), leToHost32(value.z), leToHost32(value.w));
		#endif
		iter += sizeof(uint4);
		return false;
	}
};

/**
 * @brief Network stream output data container.
 * @tparam S size of the stream output buffer in bytes
 */
template<psize S>
class StreamOutputBuffer : public StreamOutput
{
public:
	uint8 buffer[S];

	/**
	 * @brief Creates a new stream output buffer container.
	 *
	 * @param type message type string
	 * @param messageSize message size in bytes
	 * @param lengthSize message header length size in bytes
	 * @param isSystem is this system message
	 */
	StreamOutputBuffer(string_view type, psize messageSize, uint8 lengthSize, bool isSystem = false) noexcept :
		StreamOutput(type, buffer, S, messageSize, lengthSize, isSystem) { }
};

/***********************************************************************************************************************
 * @brief Server client session data container.
 */
struct ClientSession
{
	vector<uint8> datagramBuffer;
	mutex datagramLocker;
	void* streamSession = nullptr;
	uint8* messageBuffer = nullptr;
	psize messageByteCount = 0;
	uint64 clientDatagramIdx = 0;
	uint64 serverDatagramIdx = 1;
	uint8* encKey = nullptr;
	uint8* decKey = nullptr;
	void* encContext = nullptr;
	void* decContext = nullptr;
	uint32 datagramUID = 0;
	void* datagramAddress = nullptr;
	bool isAuthorized = false;

	/**
	 * @brief Returns client session stream IP address and port string.
	 */
	string getAddress() const;

	/**
	 * @brief Sends stream data to the client session. (TCP)
	 * @return The operation @ref NetsResult code.
	 *
	 * @param[in] data send data buffer
	 * @param byteCount data byte count to send
	 */
	NetsResult send(const void* data, size_t byteCount) noexcept;
	/**
	 * @brief Sends stream message to the client session. (TCP)
	 * @return The operation @ref NetsResult code.
	 * @param[in] message stream message to send
	 */
	NetsResult send(const StreamOutput& message) noexcept;

	/**
	 * @brief Resets stream session timeout time.
	 */
	void alive() noexcept;

	/**
	 * @brief Shutdowns full-duplex socket connection.
	 * @return The operation @ref NetsResult code.
	 */
	NetsResult shutdownFull() noexcept;
	/**
	 * @brief Shutdowns receive part of the full-duplex socket connection.
	 * @return The operation @ref NetsResult code.
	 */
	NetsResult shutdownReceive() noexcept;
	/**
	 * @brief Shutdowns send part of the full-duplex socket connection.
	 * @return The operation @ref NetsResult code.
	 */
	NetsResult shutdownSend() noexcept;

	/*******************************************************************************************************************
	 * Encryption functions.
	 */
	static constexpr uint8 keySize = 32; /**< 256 bits */
	static constexpr uint8 ivSize = 12;  /**< 4 bytes UID + 8 bytes counter */
	static constexpr uint8 tagSize = 16; /**< 128 bits */

	static void* createEncContext(uint8*& encKey, void*& cipher) noexcept;
	static void* createDecContext(const uint8* decKey, void*& cipher) noexcept;
	static bool updateEncDecKey(void* context, uint8* key) noexcept;
	static void destroyEncDecContext(void* context, uint8* key) noexcept;
	static void destroyCipher(void* cipher) noexcept;

	static psize packDatagram(const void* data, psize size, 
		vector<uint8>& datagramBuffer, uint32 datagramUID, uint64& datagramIdx) noexcept;
	static psize encryptDatagram(const void* plainData, psize size, void* encContext, 
		vector<uint8>& datagramBuffer, uint32 datagramUID, uint64& datagramIdx) noexcept;
	psize encryptDatagram(const void* data, psize size) noexcept;
	static psize decryptDatagram(const uint8* encData, psize size, 
		void* decContext, vector<uint8>& datagramBuffer) noexcept;
	psize decryptDatagram(const uint8* data, psize size) noexcept;
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
	virtual int onRequest(ClientSession* session, StreamInput message) { return NOT_SUPPORTED_NETS_RESULT; }
	/**
	 * @brief On message receive from the server.
	 * @details Client closes connection on this function non zero return result.
	 * @warning This function is called asynchronously from the receive thread!
	 * @param message received stream message
	 */
	virtual int onResponse(StreamInput message) { return NOT_SUPPORTED_NETS_RESULT; }
};

} // namespace garden