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

#include "garden/network.hpp"
#include "nets/stream-server.hpp"

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
NetsResult ClientSession::send(const StreamResponse& streamResponse) noexcept
{
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	return session.send(streamResponse);
}

NetsResult ClientSession::sendEncKey(string_view messageType, uint8 lengthSize) noexcept
{
	GARDEN_ASSERT(!messageType.empty());
	nets::StreamSessionView session((StreamSession_T*)streamSession);
	if (session.getSocket().getInstance() && !session.getSocket().getSslContext().getInstance())
		return SUCCESS_NETS_RESULT; // Server connection is not secure.

	vector<uint8> sendBuffer; auto messageSize = 16; // TODO: generate and write encryption key.
	StreamResponse message(messageType, sendBuffer, messageSize, lengthSize);
	return session.send(message);
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