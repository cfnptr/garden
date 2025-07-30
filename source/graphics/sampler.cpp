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

#include "garden/graphics/sampler.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace math;
using namespace garden;
using namespace garden::graphics;

static void destroyVkSampler(void* instance)
{
	auto vulkanAPI = VulkanAPI::get();
	if (vulkanAPI->forceResourceDestroy)
		vulkanAPI->device.destroySampler((VkSampler)instance);
	else
		vulkanAPI->destroyResource(GraphicsAPI::DestroyResourceType::Sampler, instance);
}

//**********************************************************************************************************************
Sampler::Sampler(const State& state) : state(state)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto samplerInfo = getVkSamplerCreateInfo(state);
		this->instance = VulkanAPI::get()->device.createSampler(samplerInfo);
	}
	else abort();
}

bool Sampler::destroy()
{
	if (!instance || busyLock > 0)
		return false;

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		destroyVkSampler(instance);
	else abort();

	return true;
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void Sampler::setDebugName(const string& name)
{
	Resource::setDebugName(name);

	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		#if GARDEN_DEBUG // Note: No GARDEN_EDITOR
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->hasDebugUtils)
			return;

		vk::DebugUtilsObjectNameInfoEXT nameInfo(vk::ObjectType::eSampler, (uint64)instance, name.c_str());
		vulkanAPI->device.setDebugUtilsObjectNameEXT(nameInfo);
		#endif
	}
	else abort();
}
#endif