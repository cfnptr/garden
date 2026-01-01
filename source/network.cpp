// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

#include "garden/network.hpp"
#include "nets/stream-server.hpp"
#include "openssl/rand.h"
#include "openssl/evp.h"

using namespace garden;

string ClientSession::getAddress() const
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.getAddress();
}

NetsResult ClientSession::send(const void* data, size_t byteCount) noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.send(data, byteCount);
}
NetsResult ClientSession::send(const StreamOutput& message) noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.send(message);
}

void ClientSession::alive() noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	session.alive();
}

NetsResult ClientSession::shutdownFull() noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.shutdown(RECEIVE_SEND_SOCKET_SHUTDOWN);
}
NetsResult ClientSession::shutdownReceive() noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.shutdown(RECEIVE_ONLY_SOCKET_SHUTDOWN);
}
NetsResult ClientSession::shutdownSend() noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.shutdown(SEND_ONLY_SOCKET_SHUTDOWN);
}

//******************************************************************************************************************
void* ClientSession::createEncContext(uint8*& encKey, void*& cipher) noexcept
{
	if (!cipher)
	{
		cipher = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
		if (!cipher)
			return NULL;
	}

	auto encContext = EVP_CIPHER_CTX_new();
	if (!encContext)
		return NULL;

	encKey = new uint8[keySize]; // Note: key is allocated for memory safety.
	if (!RAND_bytes(encKey, keySize))
	{
		EVP_CIPHER_CTX_free(encContext); free(encKey);
		return NULL;
	}

	if (!EVP_EncryptInit_ex(encContext, (EVP_CIPHER*)cipher, NULL, encKey, NULL))
	{
		EVP_CIPHER_CTX_free(encContext); free(encKey);
		return NULL;
	}
	if (!EVP_CIPHER_CTX_ctrl(encContext, EVP_CTRL_GCM_SET_IVLEN, ivSize, NULL))
	{
		EVP_CIPHER_CTX_free(encContext); free(encKey);
		return NULL;
	}
	return encContext;
}
void* ClientSession::createDecContext(const uint8* decKey, void*& cipher) noexcept
{
	if (!cipher)
	{
		cipher = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
		if (!cipher)
			return NULL;
	}

	auto encContext = EVP_CIPHER_CTX_new();
	if (!encContext)
		return NULL;

	if (!EVP_DecryptInit_ex(encContext, (EVP_CIPHER*)cipher, NULL, decKey, NULL))
	{
		EVP_CIPHER_CTX_free(encContext);
		return NULL;
	}
	if (!EVP_CIPHER_CTX_ctrl(encContext, EVP_CTRL_GCM_SET_IVLEN, ivSize, NULL))
	{
		EVP_CIPHER_CTX_free(encContext);
		return NULL;
	}
	return encContext;
}

//******************************************************************************************************************
bool ClientSession::updateEncDecKey(void* context, uint8* key) noexcept
{
	GARDEN_ASSERT(context);
	GARDEN_ASSERT(key);
	return EVP_DecryptInit_ex((EVP_CIPHER_CTX*)context, NULL, NULL, key, NULL);
}
void ClientSession::destroyEncDecContext(void* context, uint8* key) noexcept
{
	EVP_CIPHER_CTX_free((EVP_CIPHER_CTX*)context);

	if (key)
	{
		OPENSSL_cleanse(key, keySize * sizeof(uint8));
		delete[] key;
	}
}
void ClientSession::destroyCipher(void* cipher) noexcept
{
	EVP_CIPHER_free((EVP_CIPHER*)cipher);
}

psize ClientSession::packDatagram(const void* data, psize size, 
	vector<uint8>& datagramBuffer, uint32 datagramUID, uint64& datagramIdx) noexcept
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(size > 0);

	auto totalSize = size + ClientSession::ivSize;
	if (datagramBuffer.size() < totalSize)
		datagramBuffer.resize(totalSize);

	auto outBuffer = datagramBuffer.data();
	*((uint32*)outBuffer) = datagramUID;
	*((uint64*)(outBuffer + sizeof(uint32))) = hostToLE64(datagramIdx++);
	memcpy(outBuffer + ClientSession::ivSize, data, size);
	return totalSize;
}
psize ClientSession::encryptDatagram(const void* plainData, psize size, void* encContext, 
	vector<uint8>& datagramBuffer, uint32 datagramUID, uint64& datagramIdx) noexcept
{
	GARDEN_ASSERT(plainData);
	GARDEN_ASSERT(size > 0);
	GARDEN_ASSERT(encContext);

	if (datagramIdx == UINT64_MAX)
		return 0; // Overflow will spoil IV.

	auto context = (EVP_CIPHER_CTX*)encContext;
	auto maxSize = size + EVP_CIPHER_CTX_get_block_size(context) + (ivSize + tagSize);
	if (datagramBuffer.size() < maxSize)
		datagramBuffer.resize(maxSize);

	auto outBuffer = datagramBuffer.data();
	*((uint32*)outBuffer) = datagramUID;
	*((uint64*)(outBuffer + sizeof(uint32))) = hostToLE64(datagramIdx++);

	if (!EVP_EncryptInit_ex(context, NULL, NULL, NULL, outBuffer))
		return 0;

	int tmpSize;
	if (!EVP_EncryptUpdate(context, NULL, &tmpSize, outBuffer, ivSize))
		return 0;
	GARDEN_ASSERT(tmpSize == ivSize);

	auto ctrlSize = hostToLE64(size);
	if (!EVP_EncryptUpdate(context, NULL, &tmpSize, (const uint8*)&ctrlSize, sizeof(uint64)))
		return 0;
	GARDEN_ASSERT(tmpSize == sizeof(uint64));

	if (!EVP_EncryptUpdate(context, outBuffer + ivSize, &tmpSize, (const uint8*)plainData, size))
		return 0;
	GARDEN_ASSERT(tmpSize == size);

	int totalSize = ivSize + size;
	if (!EVP_EncryptFinal_ex(context, outBuffer + totalSize, &tmpSize))
		return 0;
	GARDEN_ASSERT(tmpSize == 0);

	if (!EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, tagSize, outBuffer + totalSize))
		return 0;
	return totalSize + tagSize;
}
psize ClientSession::encryptDatagram(const void* data, psize size) noexcept
{
	return encryptDatagram(data, size, encContext, datagramBuffer, datagramUID, serverDatagramIdx);
}

//******************************************************************************************************************
psize ClientSession::decryptDatagram(const uint8* encData, 
	psize size, void* decContext, vector<uint8>& datagramBuffer) noexcept
{
	GARDEN_ASSERT(encData);
	GARDEN_ASSERT(decContext);

	if (size <= ivSize + tagSize)
		return 0;

	auto context = (EVP_CIPHER_CTX*)decContext;
	if (!EVP_DecryptInit_ex(context, NULL, NULL, NULL, encData))
		return 0;

	int tmpSize;
	if (!EVP_DecryptUpdate(context, NULL, &tmpSize, encData, ivSize) || tmpSize != ivSize)
		return 0;

	auto dataSize = size - (ivSize + tagSize); auto ctrlSize = hostToLE64(dataSize);
	if (!EVP_DecryptUpdate(context, NULL, &tmpSize, (const uint8*)&ctrlSize, 
		sizeof(uint64)) || tmpSize != sizeof(uint64))
	{
		return 0;
	}

	if (datagramBuffer.size() < dataSize)
		datagramBuffer.resize(dataSize);
	auto outBuffer = datagramBuffer.data();

	if (!EVP_DecryptUpdate(context, outBuffer, &tmpSize, 
		encData + ivSize, dataSize) || tmpSize != dataSize)
	{
		return 0;
	}
	
	if (!EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, tagSize, (void*)(encData + (dataSize + ivSize))))
		return 0;
	if (!EVP_DecryptFinal_ex(context, outBuffer + dataSize, &tmpSize) || tmpSize != 0)
		return 0;
	return dataSize;
}
psize ClientSession::decryptDatagram(const uint8* data, psize size) noexcept
{
	return decryptDatagram(data, size, decContext, datagramBuffer);
}