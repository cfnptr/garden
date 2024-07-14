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
 * @brief Common mesh rendering functions.
 */

#pragma once
#include "garden/system/thread.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/transform.hpp"
#include "math/aabb.hpp"

#include <utility>

namespace garden
{

using namespace garden::graphics;

/**
 * @brief Mesh render types.
 */
enum class MeshRenderType : uint8
{
	Opaque, Translucent, OpaqueShadow, TranslucentShadow, Count
};

/***********************************************************************************************************************
 * @brief General mesh rendering data container.
 */
struct MeshRenderComponent : public Component
{
	Aabb aabb = Aabb::one; /**< Mesh axis aligned bounding box. */
	bool isEnabled = true; /**< Is mesh should be rendered. */
};

/**
 * @brief Mesh render system interface.
 */
class IMeshRenderSystem
{
protected:
	virtual bool isDrawReady() = 0;
	virtual void prepareDraw(const float4x4& viewProj, uint32 drawCount) { }
	virtual void beginDrawAsync(int32 taskIndex) { }
	virtual void drawAsync(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) = 0;
	virtual void endDrawAsync(uint32 drawCount, int32 taskIndex) { }
	virtual void finalizeDraw(const float4x4& viewProj, uint32 drawCount) { }

	friend class MeshRenderSystem;
public:
	virtual MeshRenderType getMeshRenderType() const = 0;
	virtual const LinearPool<MeshRenderComponent>& getMeshComponentPool() const = 0;
	virtual psize getMeshComponentSize() const = 0;
};
/**
 * @brief Mesh shadow render system interface.
 */
class IShadowMeshRenderSystem
{
protected:
	virtual uint32 getShadowPassCount() = 0;
	virtual bool prepareShadowRender(uint32 passIndex, float4x4& viewProj, float3& cameraOffset) = 0;
	virtual void beginShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
	virtual void endShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
	friend class MeshRenderSystem;
};

/***********************************************************************************************************************
 * @brief General mesh rendering system.
 */
class MeshRenderSystem final : public System
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
	ThreadSystem* threadSystem = nullptr;
	vector<OpaqueBuffer> opaqueBuffers;
	vector<TranslucentBuffer> translucentBuffers;
	vector<TranslucentItem> translucentItems;
	vector<uint32> translucentIndices;
	vector<IMeshRenderSystem*> meshSystems;
	volatile int64 translucentIndex = 0;
	uint32 opaqueBufferCount = 0;
	uint32 translucentBufferCount = 0;
	bool asyncRecording = false;
	bool asyncPreparing = false;

	/**
	 * @brief Creates a new mesh rendering system instance.
	 * 
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param useAsyncPreparing use multithreaded render items preparing
	 */
	MeshRenderSystem(bool useAsyncRecording = true, bool useAsyncPreparing = true);
	/**
	 * @brief Destroys mesh rendering system instance.
	 */
	~MeshRenderSystem() final;

	void prepareSystems();
	void prepareItems(const float4x4& viewProj, const float3& cameraOffset,
		uint8 frustumPlaneCount, MeshRenderType opaqueType, MeshRenderType translucentType);
	void renderOpaque(const float4x4& viewProj);
	void renderTranslucent(const float4x4& viewProj);
	void renderShadows();

	void init();
	void deinit();
	void preForwardRender();
	void forwardRender();
	void preDeferredRender();
	void deferredRender();
	void hdrRender();

	friend class ecsm::Manager;
	friend class GizmosEditorSystem;
	friend class SelectorEditorSystem;
public:
	bool useAsyncRecording() const noexcept { return asyncRecording; }
	bool useAsyncPreparing() const noexcept { return asyncPreparing; }
};

} // namespace garden