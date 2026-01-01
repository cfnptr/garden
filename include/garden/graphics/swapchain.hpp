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

/***********************************************************************************************************************
 * @file
 * @brief Common graphics swapchain functions.
 */

#pragma once
#include "garden/thread-pool.hpp"
#include "garden/graphics/image.hpp"

namespace garden::graphics
{

/**
 * @brief Optimal swapchain sync primitive count.
 */
constexpr uint8 inFlightCount = 2;

/**
 * @brief Base graphics swapchain class.
 * 
 * @details
 * Swapchain is a set of buffers (usually two or more) used to manage the process of displaying rendered images on the 
 * screen. The swapchain's main role is to ensure smooth rendering and prevent visual artifacts like tearing by 
 * coordinating the display of frames to the screen. The swapchain contains multiple buffers, one of these buffers is 
 * actively being displayed on the screen (front buffer), while others are used to prepare or render the next frame 
 * (back buffers). The rendering pipeline renders frames to a back buffer, and once a frame is complete, the swapchain 
 * swaps this buffer with the front buffer. This "swap" is where the name comes from. Once the back buffer becomes the 
 * front buffer, it can be displayed on the screen. Swapchains work with synchronization techniques like vertical 
 * synchronization (V-Sync) to make sure buffer swapping aligns with the display's refresh rate, preventing screen t
 * earing. When VSync is enabled, swapping happens at the start of a new refresh cycle.
 * 
 * @warning Use graphics swapchain directly with caution!
 */
class Swapchain
{
protected:
	vector<ID<Image>> images;
	ThreadPool* threadPool = nullptr;
	uint2 framebufferSize = uint2::zero;
	uint32 imageIndex = 0;
	uint32 inFlightIndex = 0;
	bool vsync = false;
	bool tripleBuffering = false;

	Swapchain(bool useVsync, bool useTripleBuffering) noexcept : 
		vsync(useVsync), tripleBuffering(useTripleBuffering) { }
public:
	/**
	 * @brief Destroys swapchain instance.
	 */
	virtual ~Swapchain() { }

	/**
	 * @brief Returns swapchain rendering image array.
	 */
	const vector<ID<Image>>& getImages() const noexcept { return images; }
	/**
	 * @brief Returns swapchain rendering image count.
	 */
	uint32 getImageCount() const noexcept { return (uint32)images.size(); }
	/**
	 * @brief Returns current (front) swapchain rendering image.
	 */
	ID<Image> getCurrentImage() const noexcept { return images[imageIndex]; }
	/**
	 * @brief Returns current (front) swapchain image index.
	 */
	uint32 getImageIndex() const noexcept { return imageIndex; }
	/**
	 * @brief Returns swapchain framebuffer size.
	 */
	uint2 getFramebufferSize() const noexcept { return framebufferSize; }
	/**
	 * @brief Returns current in-flight frame index.
	 */
	uint32 getInFlightIndex() const noexcept { return inFlightIndex; }
	/**
	 * @brief Does swapchain use vertical synchronization. (V-Sync)
	 */
	bool useVsync() const noexcept { return vsync; }
	/**
	 * @brief Does swapchain use triple buffering. (3 framebuffers)
	 */
	bool useTripleBuffering() const noexcept { return tripleBuffering; }

	/**
	 * @brief Recreates swapchain rendering buffers.
	 * 
	 * @param framebufferSize new swapchain framebuffer size
	 * @param useVsync use vertical synchronization (V-Sync)
	 * @param useTripleBuffering use triple buffering (3 framebuffers)
	 */
	virtual void recreate(uint2 framebufferSize, bool useVsync, bool useTripleBuffering) = 0;
	/**
	 * @brief Acquires next (front) swapchain rendering buffer.
	 * @param[in,out] threadPool async recording thread pool instance or null
	 * @return True on success, or false if swapchain is out of date.
	 */
	virtual bool acquireNextImage(ThreadPool* threadPool) = 0;
	/**
	 * @brief Submits current (front) swapchain rendering buffer.
	 */
	virtual void submit() = 0;
	/**
	 * @brief Presents current (front) swapchain rendering buffer.
	 * @return True on success, or false if swapchain is out of date.
	 */
	virtual bool present() = 0;
};

} // namespace garden::graphics