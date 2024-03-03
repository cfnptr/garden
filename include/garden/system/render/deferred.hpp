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

/*
#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

// G-Buffer structure.
// 0) SrgbR8G8B8A8 (Base Color, Metallic)
// 1) UnormR10G10B10A2 (Encoded Normal, Reflectance)
// 2) UnormR8G8B8A8 (Emissive, Roughness)
#define G_BUFFER_COUNT 3

using namespace garden;
using namespace garden::graphics;
class DeferredRenderSystem;

//--------------------------------------------------------------------------------------------------
enum class MeshRenderType : uint8
{
	Opaque, Translucent, OpaqueShadow, TranslucentShadow, Count
};

//--------------------------------------------------------------------------------------------------
class IDeferredRenderSystem
{
protected:
	virtual void deferredRender() { }
	virtual void preHdrRender() { }
	virtual void hdrRender() { }
	virtual void preLdrRender() { }
	virtual void ldrRender() { }
	virtual void preSwapchainRender() { }
	friend class DeferredRenderSystem;
public:
	DeferredRenderSystem* getDeferredSystem() noexcept
	{
		GARDEN_ASSERT(deferredSystem);
		return deferredSystem;
	}
	const DeferredRenderSystem* getDeferredSystem() const noexcept
	{
		GARDEN_ASSERT(deferredSystem);
		return deferredSystem;
	}
};

//--------------------------------------------------------------------------------------------------
class DeferredRenderSystem final : public System, public IRenderSystem
{
	ID<Image> gBuffers[G_BUFFER_COUNT] = {};
	ID<Image> depthBuffer = {};
	ID<Image> hdrBuffer = {};
	ID<Image> ldrBuffer = {};
	ID<Framebuffer> gFramebuffer = {};
	ID<Framebuffer> hdrFramebuffer = {};
	ID<Framebuffer> ldrFramebuffer = {};
	ID<Framebuffer> toneMappingFramebuffer = {};
	int2 framebufferSize = int2(0);
	float renderScale = 1.0f;
	bool isAsync = false;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	DeferredRenderSystem(bool _isAsync) : isAsync(_isAsync) { }

	void initialize() final;
	void terminate() final;
	void render() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
	friend class DeferredEditorSystem;
public:
	bool runSwapchainPass = true;

	int2 getFramebufferSize() const noexcept { return framebufferSize; }
	bool isRenderAsync() const noexcept { return isAsync; }
	float getRenderScale() const noexcept { return renderScale; }
	void setRenderScale(float renderScale);

	ID<Image>* getGBuffers();
	ID<Image> getDepthBuffer();
	ID<Image> getHdrBuffer();
	ID<Image> getLdrBuffer();

	ID<Framebuffer> getGFramebuffer();
	ID<Framebuffer> getHdrFramebuffer();
	ID<Framebuffer> getLdrFramebuffer();

	#if GARDEN_EDITOR
	ID<Framebuffer> getEditorFramebuffer();
	#endif
};

} // namespace garden
*/