//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/graphics/resource.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
bool Resource::isBusy() noexcept
{
	if (lastFrameTime > GraphicsAPI::frameCommandBuffer.getBusyTime())
		lastFrameTime = GraphicsAPI::frameCommandBuffer.getBusyTime();
	if (lastTransferTime > GraphicsAPI::transferCommandBuffer.getBusyTime())
		lastTransferTime = GraphicsAPI::transferCommandBuffer.getBusyTime();
	if (lastComputeTime > GraphicsAPI::computeCommandBuffer.getBusyTime())
		lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
	if (lastGraphicsTime > GraphicsAPI::graphicsCommandBuffer.getBusyTime())
		lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();

	// Note: lastFrameTime <= GARDEN_FRAME_LAG
	// because we are incrementing in the same frame.
	
	return !instance || (GraphicsAPI::isRunning &&
		(GraphicsAPI::frameCommandBuffer.getBusyTime() - lastFrameTime <= GARDEN_FRAME_LAG ||
		GraphicsAPI::transferCommandBuffer.getBusyTime() == lastTransferTime ||
		GraphicsAPI::computeCommandBuffer.getBusyTime() == lastComputeTime ||
		GraphicsAPI::graphicsCommandBuffer.getBusyTime() == lastGraphicsTime));
}
bool Resource::isReady() noexcept
{
	if (lastTransferTime > GraphicsAPI::transferCommandBuffer.getBusyTime())
		lastTransferTime = GraphicsAPI::transferCommandBuffer.getBusyTime();
	if (lastComputeTime > GraphicsAPI::computeCommandBuffer.getBusyTime())
		lastComputeTime = GraphicsAPI::computeCommandBuffer.getBusyTime();
	if (lastGraphicsTime > GraphicsAPI::graphicsCommandBuffer.getBusyTime())
		lastGraphicsTime = GraphicsAPI::graphicsCommandBuffer.getBusyTime();
	
	return instance &&
		GraphicsAPI::transferCommandBuffer.getBusyTime() != lastTransferTime &&
		GraphicsAPI::computeCommandBuffer.getBusyTime() != lastComputeTime &&
		GraphicsAPI::graphicsCommandBuffer.getBusyTime() != lastGraphicsTime;
}