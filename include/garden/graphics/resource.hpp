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

/***********************************************************************************************************************
 * @file
 * @brief Common graphics resource functions.
 */

#pragma once
#include "garden/defines.hpp"

namespace garden::graphics
{

using namespace std;
using namespace math;
class ResourceExt;

/***********************************************************************************************************************
 * @brief Graphics resource type.
 */
enum class ResourceType : uint8
{
	Buffer, Image, ImageView, Framebuffer, GraphicsPipeline,
	ComputePipeline, DescriptorSet, Count
};

/**
 * @brief Graphics resource base class.
 * 
 * @details 
 * Various types of objects that represent GPU data used in rendering and computation tasks. Resources allow 
 * applications to define, store, and manipulate the data necessary for graphics rendering and compute operations.
 */
class Resource
{
protected:
	void* instance = nullptr;
	uint32 readyLock = 0;
	
	#if GARDEN_DEBUG || GARDEN_EDITOR
	#define UNNAMED_RESOURCE "unnamed"
	string debugName = UNNAMED_RESOURCE;
	#endif

	// Use GraphicsSystem to create, destroy and access graphics resources.

	Resource() = default;
	virtual bool destroy() = 0;

	friend class ResourceExt;
public:
	/**
	 * @brief Returns true if resource is ready for graphics rendering.
	 * @details Graphics resource is loaded and transferred.
	 */
	bool isReady() const noexcept { return instance && readyLock < 1; }

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Returns resource debug name. (Debug Only)
	 * @details Also visible inside GPU profilers. (RenderDoc, Nsight, Xcode...)
	 */
	const string& getDebugName() const noexcept { return debugName; }
	/**
	 * @brief Sets resource debug name. (Debug Only)
	 * @param[in] name debug resource name
	 */
	virtual void setDebugName(const string& name)
	{
		GARDEN_ASSERT(!name.empty());
		debugName = name;
	}
	#endif
};

/**
 * @brief Graphics resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class ResourceExt final
{
public:
	/**
	 * @brief Returns resource native instance.
	 * @warning In most cases you should use @ref Resource functions.
	 * @param[in] resource target resource instance
	 */
	static void*& getInstance(Resource& resource) noexcept { return resource.instance; }
	/**
	 * @brief Returns resource ready lock.
	 * @warning In most cases you should use @ref Resource functions.
	 * @param[in] resource target resource instance
	 */
	static uint32& getReadyLock(Resource& resource) noexcept { return resource.readyLock; }
};

} // namespace garden::graphics