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

#include "garden/graphics/lod.hpp"
#include "garden/defines.hpp"
#include "garden/graphics/api.hpp"
#include "garden/graphics/buffer.hpp"
#include "garden/system/graphics.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
LodBuffer::LodBuffer(uint32 count, float maxDistanceSq) : 
	readyStates(count), vertexBuffers(count), indexBuffers(count), splits(count)
{
	auto graphicsAPI = GraphicsAPI::get();
	for (uint32 i = 0; i < count; i++)
	{
		auto version = graphicsAPI->imageVersion++;
		vertexBuffers[i] = graphicsAPI->bufferPool.create(Buffer::Bind::Vertex | Buffer::Bind::TransferDst,
			Buffer::Access::None, Buffer::Usage::PreferGPU, Buffer::Strategy::Size, version);
		version = graphicsAPI->imageVersion++;
		indexBuffers[i] = graphicsAPI->bufferPool.create(Buffer::Bind::Vertex | Buffer::Bind::TransferDst,
			Buffer::Access::None, Buffer::Usage::PreferGPU, Buffer::Strategy::Size, version);
		splits[i] = (i + 1) / (float)count;
	}
}
void LodBuffer::destroy()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (auto buffer : vertexBuffers)
		graphicsSystem->destroy(buffer);
	vertexBuffers.clear();
	for (auto buffer : indexBuffers)
		graphicsSystem->destroy(buffer);
	indexBuffers.clear();
}

//**********************************************************************************************************************
bool LodBuffer::getLevel(float distanceSq, ID<Buffer>& vertexBuffer, ID<Buffer>& indexBuffer) const
{
	GARDEN_ASSERT(distanceSq >= 0.0f);
	GARDEN_ASSERT(!splits.empty());

	auto count = (uint32)splits.size();
	for (uint32 i = 0; i < count; i++)
	{
		if (distanceSq > splits[i] || readyStates[i] < 2)
			continue;

		vertexBuffer = ID<Buffer>(vertexBuffers[i]);
		indexBuffer = ID<Buffer>(indexBuffers[i]);
		return true;
	}

	auto lastIndex = count - 1;
	vertexBuffer = ID<Buffer>(vertexBuffers[lastIndex]);
	indexBuffer = ID<Buffer>(indexBuffers[lastIndex]);
	return readyStates[lastIndex] > 1;
}

void LodBuffer::addLevel(uint32 level, float splitSq, ID<Buffer> vertexBuffer, ID<Buffer> indexBuffer)
{
	GARDEN_ASSERT(level <= splits.size());
	GARDEN_ASSERT(splitSq >= 0.0f);
	GARDEN_ASSERT(vertexBuffer);
	GARDEN_ASSERT(indexBuffer);
	GARDEN_ASSERT(isReady()); // Can't modify when async loading.

	readyStates.emplace(readyStates.begin() + level, 0);
	splits.emplace(splits.begin() + level, splitSq);
	vertexBuffers.emplace(vertexBuffers.begin() + level, vertexBuffer);
	indexBuffers.emplace(indexBuffers.begin() + level, indexBuffer);
}
void LodBuffer::removeLevel(uint32 level)
{
	GARDEN_ASSERT(level < splits.size());
	GARDEN_ASSERT(isReady()); // Can't modify when async loading.
	
	readyStates.erase(readyStates.begin() + level);
	splits.erase(splits.begin() + level);
	vertexBuffers.erase(vertexBuffers.begin() + level);
	indexBuffers.erase(indexBuffers.begin() + level);
}

void LodBuffer::setSplit(uint32 level, float splitSq)
{
	GARDEN_ASSERT(level < splits.size());
	GARDEN_ASSERT(splitSq >= 0.0f);
	splits[level] = splitSq;
}
void LodBuffer::updateReadyState(uint32 level, ID<Buffer> loadedBuffer)
{
	GARDEN_ASSERT(level < splits.size());
	GARDEN_ASSERT(vertexBuffers[level] == loadedBuffer || indexBuffers[level] == loadedBuffer);
	readyStates[level]++;
}