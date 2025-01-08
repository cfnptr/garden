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

/***********************************************************************************************************************
 * @file
 * @brief Common mesh rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"
#include "garden/system/transform.hpp"
#include "math/aabb.hpp"

#include <utility>

namespace garden
{

/**
 * @brief Common mesh render types.
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
protected:
	bool visible = false;
public:
	/**
	 * @brief Is mesh visible on camera after last frustum culling.
	 */
	bool isVisible() const noexcept { return visible; }
	/**
	 * @brief Set mesh visible on camera.
	 */
	void setVisible(bool isVisible) noexcept { visible = isVisible; }
};

/**
 * @brief Mesh render system interface.
 */
class IMeshRenderSystem
{
protected:
	/**
	 * @brief Is mesh system ready for rendering. (All resources loaded, etc.)
	 */
	virtual bool isDrawReady() { return true; }
	/**
	 * @brief Prepares data required for mesh rendering.
	 *
	 * @param[in] viewProj camera view * projection matrix
	 * @param[in] drawCount total mesh draw item count
	 */
	virtual void prepareDraw(const float4x4& viewProj, uint32 drawCount) { }
	/**
	 * @brief Begins mesh drawing asynchronously.
	 * @warning Be careful with multithreaded code!
	 * @param taskIndex task index in the thread pool
	 */
	virtual void beginDrawAsync(int32 taskIndex) { }
	/**
	 * @brief Draws mesh item asynchronously.
	 * @warning Be careful with multithreaded code!
	 * 
	 * @param[in,out] meshRenderView target mesh render item
	 * @param[in] viewProj camera view * projection matrix
	 * @param[in] model mesh model matrix (position, scale, rotation, etc.)
	 * @param drawIndex mesh item draw index (sorted)
	 * @param taskIndex task index in the thread pool
	 */
	virtual void drawAsync(MeshRenderComponent* meshRenderView, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) = 0;
	/**
	 * @brief Ends mesh drawing asynchronously.
	 * @warning Be careful with multithreaded code!
	 * 
	 * @param[in] drawCount total mesh draw item count
	 * @param taskIndex task index in the thread pool
	 */
	virtual void endDrawAsync(uint32 drawCount, int32 taskIndex) { }
	/**
	 * @brief Finalizes data used for mesh rendering.
	 * @warning Be careful with multithreaded code!
	 * 
	 * @param[in] viewProj camera view * projection matrix
	 * @param[in] drawCount total mesh draw item count
	 */
	virtual void finalizeDraw(const float4x4& viewProj, uint32 drawCount) { }

	friend class MeshRenderSystem;
public:
	/**
	 * @brief Returns system mesh render type. (Opaque, translucent, etc.)
	 */
	virtual MeshRenderType getMeshRenderType() const { return MeshRenderType::Opaque; }
	/**
	 * @brief Returns system mesh component pool.
	 */
	virtual LinearPool<MeshRenderComponent>& getMeshComponentPool() = 0;
	/**
	 * @brief Returns system mesh component size in bytes.
	 */
	virtual psize getMeshComponentSize() const = 0;
};

/***********************************************************************************************************************
 * @brief Mesh shadow render system interface.
 */
class IShadowMeshRenderSystem
{
protected:
	/**
	 * @brief Returns mesh shadow render pass count.
	 */
	virtual uint32 getShadowPassCount() = 0;
	/**
	 * @brief Prepares all required data for mesh shadow rendering.
	 * 
	 * @param passIndex shadow render pass index
	 * @param[in] viewProj camera view * projection matrix
	 * @param[in] cameraOffset camera offset in the space
	 */
	virtual bool prepareShadowRender(uint32 passIndex, float4x4& viewProj, float3& cameraOffset) = 0;
	/**
	 * @brief Begins mesh shadow pass rendering.
	 * 
	 * @param passIndex shadow render pass index
	 * @param renderType shadow mesh render type
	 */
	virtual void beginShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
	/**
	 * @brief Ends mesh shadow pass rendering.
	 * 
	 * @param passIndex shadow render pass index
	 * @param renderType shadow mesh render type
	 */
	virtual void endShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;

	friend class MeshRenderSystem;
};

/***********************************************************************************************************************
 * @brief General mesh rendering system.
 */
class MeshRenderSystem final : public System
{
public:
	struct alignas(64) OpaqueMesh final
	{
		MeshRenderComponent* renderView = nullptr;
		float4x3 model = float4x3(0.0f);
		float distance2 = 0.0f;
	private:
		uint32 _alignment = 0;
	public:
		bool operator<(const OpaqueMesh& m) const noexcept
		{
			return distance2 < m.distance2;
		}
	};
	struct alignas(64) TranslucentMesh final
	{
		MeshRenderComponent* renderView = nullptr;
		float4x3 model = float4x3(0.0f);
		float distance2 = 0.0f;
		uint32 bufferIndex = 0;

		bool operator<(const TranslucentMesh& m) const noexcept
		{
			return distance2 > m.distance2;
		}
	};

	struct MeshBuffer
	{
		IMeshRenderSystem* meshSystem = nullptr;
		atomic<uint32>* drawCount = nullptr;

		MeshBuffer() : drawCount(new atomic<uint32>()) { }
		~MeshBuffer() { delete drawCount; }
	};
	struct OpaqueBuffer final : public MeshBuffer
	{
		vector<vector<OpaqueMesh>> threadMeshes;
		vector<OpaqueMesh> combinedMeshes;
	};
	struct TranslucentBuffer final : MeshBuffer { };
private:
	vector<OpaqueBuffer> opaqueBuffers;
	vector<TranslucentBuffer> translucentBuffers;
	vector<TranslucentMesh> transCombinedMeshes;
	vector<vector<TranslucentMesh>> transThreadMeshes;
	vector<IMeshRenderSystem*> meshSystems;
	atomic<uint32> translucentIndex = 0;
	uint32 opaqueBufferCount = 0;
	uint32 translucentBufferCount = 0;
	bool asyncRecording = false;
	bool asyncPreparing = false;

	/**
	 * @brief Creates a new mesh rendering system instance.
	 * 
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param useAsyncPreparing use multithreaded render meshes preparing
	 */
	MeshRenderSystem(bool useAsyncRecording = true, bool useAsyncPreparing = true);
	/**
	 * @brief Destroys mesh rendering system instance.
	 */
	~MeshRenderSystem() final;

	void prepareSystems();
	void sortMeshes();
	void prepareMeshes(const float4x4& viewProj, const float3& cameraOffset,
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
	void translucentRender();

	friend class ecsm::Manager;
	friend class GizmosEditorSystem;
	friend class SelectorEditorSystem;
public:
	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }
	/**
	 * @brief Use multithreaded render meshes preparing.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncPreparing() const noexcept { return asyncPreparing; }
};

} // namespace garden