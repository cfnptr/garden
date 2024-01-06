//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

#pragma once
#include "garden/defines.hpp"
#include "math/types.hpp"

#include <string>
#include <stdexcept>
#include <string_view>

namespace garden::graphics
{

using namespace std;
using namespace math;
class ResourceExt;

enum class ResourceType : uint8
{
	Buffer, Image, ImageView, Framebuffer, GraphicsPipeline,
	ComputePipeline, DescriptorSet, Count
};

class Resource
{
protected:
	void* instance = nullptr;
	uint32 readyLock = 0;
	
	#if GARDEN_DEBUG || GARDEN_EDITOR
	#define UNNAMED_RESOURCE "unnamed"
	string debugName = UNNAMED_RESOURCE;
	#endif

	Resource() = default;
	virtual bool destroy() = 0;

	friend class ResourceExt;
public:
	bool isReady() const noexcept { return instance && readyLock < 1; }

	#if GARDEN_DEBUG || GARDEN_EDITOR
	const string& getDebugName() const noexcept { return debugName; }
	virtual void setDebugName(const string& name)
	{
		GARDEN_ASSERT(!name.empty());
		debugName = name;
	}
	#endif
};

class ResourceExt final
{
public:
	static void*& getInstance(Resource& resource) noexcept { return resource.instance; }
};

} // garden::graphics