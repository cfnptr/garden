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
	if (lastFrameTime > Vulkan::frameCommandBuffer.getBusyTime())
		lastFrameTime = Vulkan::frameCommandBuffer.getBusyTime();
	if (lastTransferTime > Vulkan::transferCommandBuffer.getBusyTime())
		lastTransferTime = Vulkan::transferCommandBuffer.getBusyTime();
	if (lastComputeTime > Vulkan::computeCommandBuffer.getBusyTime())
		lastComputeTime = Vulkan::computeCommandBuffer.getBusyTime();
	if (lastGraphicsTime > Vulkan::graphicsCommandBuffer.getBusyTime())
		lastGraphicsTime = Vulkan::graphicsCommandBuffer.getBusyTime();

	// Note: lastFrameTime <= GARDEN_FRAME_LAG
	// because we are incrementing in the same frame.
	
	return !instance || Vulkan::isRunning &&
		(Vulkan::frameCommandBuffer.getBusyTime() - lastFrameTime <= GARDEN_FRAME_LAG ||
		Vulkan::transferCommandBuffer.getBusyTime() == lastTransferTime ||
		Vulkan::computeCommandBuffer.getBusyTime() == lastComputeTime ||
		Vulkan::graphicsCommandBuffer.getBusyTime() == lastGraphicsTime);
}
bool Resource::isReady() noexcept
{
	if (lastTransferTime > Vulkan::transferCommandBuffer.getBusyTime())
		lastTransferTime = Vulkan::transferCommandBuffer.getBusyTime();
	if (lastComputeTime > Vulkan::computeCommandBuffer.getBusyTime())
		lastComputeTime = Vulkan::computeCommandBuffer.getBusyTime();
	if (lastGraphicsTime > Vulkan::graphicsCommandBuffer.getBusyTime())
		lastGraphicsTime = Vulkan::graphicsCommandBuffer.getBusyTime();
	
	return instance &&
		Vulkan::transferCommandBuffer.getBusyTime() != lastTransferTime &&
		Vulkan::computeCommandBuffer.getBusyTime() != lastComputeTime &&
		Vulkan::graphicsCommandBuffer.getBusyTime() != lastGraphicsTime;
}