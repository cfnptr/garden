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

#pragma once
#include "math/aabb.hpp"
#include "garden/system/graphics/deferred.hpp"
#include <utility>

namespace garden
{

using namespace garden;
using namespace garden::graphics;
class MeshRenderSystem;
class IMeshRenderSystem;

//--------------------------------------------------------------------------------------------------
struct MeshRenderComponent : public Component
{
protected:
	ID<Entity> entity = {};
	ID<TransformComponent> transform = {};
	friend class MeshRenderSystem;
public:
	Aabb aabb = Aabb::one;
	bool isEnabled = true;

	ID<Entity> getEntity() const noexcept { return entity; }
	ID<TransformComponent> getTransform() const noexcept { return transform; }
};

//--------------------------------------------------------------------------------------------------
class IMeshRenderSystem
{
protected:
	virtual bool isDrawReady() = 0;
	virtual void prepareDraw(const float4x4& viewProj,
		ID<Framebuffer> framebuffer, uint32 drawCount) { }
	// WARNING: can be called from multiple threads asynchronously.
	virtual void beginDraw(int32 taskIndex) { }
	// WARNING: can be called from multiple threads asynchronously.
	virtual void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) = 0;
	// WARNING: can be called from multiple threads asynchronously.
	virtual void endDraw(uint32 drawCount, int32 taskIndex) { }
	virtual void finalizeDraw(const float4x4& viewProj,
		ID<Framebuffer> framebuffer, uint32 drawCount) { }
	friend class MeshRenderSystem;
public:
	virtual MeshRenderType getMeshRenderType() const = 0;
	virtual const LinearPool<MeshRenderComponent>& getMeshComponentPool() const = 0;
	virtual psize getMeshComponentSize() const = 0;
};

//--------------------------------------------------------------------------------------------------
class IShadowMeshRenderSystem
{
protected:
	virtual uint32 getShadowPassCount() = 0;
	virtual bool prepareShadowRender(uint32 passIndex, float4x4& viewProj,
		float3& cameraOffset, ID<Framebuffer>& framebuffer) = 0;
	virtual void beginShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
	virtual void endShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
	friend class MeshRenderSystem;
};

//--------------------------------------------------------------------------------------------------
class MeshRenderSystem final : public System, public IRenderSystem, public IDeferredRenderSystem
{
public:
	struct RenderItem
	{
		MeshRenderComponent* meshRender = nullptr;
		float4x4 model = float4x4(0.0f);
		float distance2 = 0.0f;
	};
	struct TranslucentItem final : public RenderItem
	{
		uint32 bufferIndex = 0;
	};
	struct OpaqueBuffer final
	{
		IMeshRenderSystem* meshSystem = nullptr;
		vector<RenderItem> items;
		vector<uint32> indices;
		volatile int64 drawCount;
	};
	struct TranslucentBuffer final
	{
		IMeshRenderSystem* meshSystem = nullptr;
		volatile int64 drawCount;
	};
private:
	TransformSystem* transformSystem = nullptr;
	ThreadSystem* threadSystem = nullptr;
	vector<IShadowMeshRenderSystem*> shadowSystems;
	vector<OpaqueBuffer> opaqueBuffers;
	vector<TranslucentBuffer> translucentBuffers;
	vector<TranslucentItem> translucentItems;
	vector<uint32> translucentIndices;
	volatile int64 translucentIndex;
	uint32 opaqueBufferCount = 0;
	uint32 translucentBufferCount = 0;
	bool isAsync = false;

	#if GARDEN_EDITOR
	void* selectorEditor = nullptr;
	void* gizmosEditor = nullptr;
	#endif

	MeshRenderSystem(bool _isAsync) : isAsync(_isAsync) { }

	void prepareItems(const float4x4& viewProj, const float3& cameraPosition,
		const vector<Manager::SubsystemData>& subsystems, MeshRenderType opaqueType,
		MeshRenderType translucentType, uint8 frustumPlaneCount, const float3& cameraOffset);
	void renderOpaqueItems(const float4x4& viewProj, ID<Framebuffer> framebuffer);
	void renderTranslucentItems(const float4x4& viewProj, ID<Framebuffer> framebuffer);

	void initialize() final;
	void terminate() final;
	void render() final;
	void deferredRender() final;
	void hdrRender() final;
	void preSwapchainRender() final;

	friend class ecsm::Manager;
	friend class GizmosEditor;
	friend class SelectorEditor;
public:
	bool isDrawAsync() const noexcept { return isAsync; }

	#if GARDEN_EDITOR
	void* getSelectorEditor() noexcept { return selectorEditor; }
	void* getGizmosEditor() noexcept { return gizmosEditor; }
	#endif

	void registerShadowSystem(IShadowMeshRenderSystem* system)
	{
		GARDEN_ASSERT(system);
		shadowSystems.push_back(system);
	}
};

} // garden