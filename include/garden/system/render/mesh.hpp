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
#include "math/aabb.hpp"

namespace garden
{

/**
 * @brief Common mesh render types.
 */
enum class MeshRenderType : uint8
{
	Color,       /**< Opaque color only rendering. (Directly to the HDR buffer)  */
	Opaque,      /**< Blocks all light from passing through. (Faster to compute) */
	Translucent, /**< Allows some light to pass through, enabling partial transparency. */
	OIT,         /**< Order independent transparency. (Faster than Translucent type) */
	Refracted,   /**< Refracted or absorbed light rendering. */
	TransDepth,  /**< Translucent depth only rendering. (Useful for ray tracing) */
	Count        /**< Common mesh render type count. */
};

/***********************************************************************************************************************
 * @brief General mesh rendering data container.
 */
struct MeshRenderComponent : public Component
{
protected:
	uint16 _alignment0 = 0;
	bool visible = false;
public:
	bool isEnabled = true; /**< Is mesh should be rendered. */
	Aabb aabb = Aabb::one; /**< Mesh axis aligned bounding box. */

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
	 * @param shadowPass current shadow pass index (light pass = -1)
	 */
	virtual bool isDrawReady(int8 shadowPass) = 0;
	/**
	 * @brief Prepares data required for mesh rendering.
	 *
	 * @param[in] viewProj camera view * projection matrix
	 * @param drawCount total mesh draw item count
	 * @param shadowPass current shadow pass index (light pass = -1)
	 */
	virtual void prepareDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass) { }
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
	virtual void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 drawIndex, int32 taskIndex) = 0;
	/**
	 * @brief Ends mesh drawing asynchronously.
	 * @warning Be careful with multithreaded code!
	 * 
	 * @param drawCount total mesh draw item count
	 * @param taskIndex task index in the thread pool
	 */
	virtual void endDrawAsync(uint32 drawCount, int32 taskIndex) { }
	/**
	 * @brief Finalizes data used for mesh rendering.
	 * @warning Be careful with multithreaded code!
	 * 
	 * @param[in] viewProj camera view * projection matrix
	 * @param drawCount total mesh draw item count
	 * @param shadowPass current shadow pass index (light pass = -1)
	 */
	virtual void finalizeDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass) { }
	/**
	 * @brief Cleans up data used for mesh rendering.
	 * @warning Be careful with multithreaded code!
	 */
	virtual void renderCleanup() { }

	friend class MeshRenderSystem;
public:
	/**
	 * @brief Returns system mesh render type. (Opaque, translucent / transparent, OIT, etc.)
	 */
	virtual MeshRenderType getMeshRenderType() const = 0;
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
	virtual uint8 getShadowPassCount() = 0;
	/**
	 * @brief Prepares all required data for mesh shadow rendering.
	 * 
	 * @param passIndex shadow render pass index
	 * @param[out] viewProj camera view * projection matrix
	 * @param[out] cameraOffset camera offset in 3D space
	 */
	virtual bool prepareShadowRender(uint32 passIndex, f32x4x4& viewProj, f32x4& cameraOffset) = 0;
	/**
	 * @brief Begins mesh shadow pass rendering.
	 * 
	 * @param passIndex shadow render pass index
	 * @param renderType shadow mesh render type
	 */
	virtual bool beginShadowRender(uint32 passIndex, MeshRenderType renderType) = 0;
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
class MeshRenderSystem final : public System, public Singleton<MeshRenderSystem>
{
public:
	struct alignas(64) UnsortedMesh final
	{
		MeshRenderComponent* renderView = nullptr;
		float4x3 model = float4x3::zero;
		float distanceSq = 0.0f;
	private:
		uint32 _alignment = 0;
	public:
		bool operator<(const UnsortedMesh& m) const noexcept { return distanceSq < m.distanceSq; }
	};
	struct alignas(64) SortedMesh final
	{
		MeshRenderComponent* renderView = nullptr;
		float4x3 model = float4x3::zero;
		float distanceSq = 0.0f;
		uint32 bufferIndex = 0;

		bool operator<(const SortedMesh& m) const noexcept { return distanceSq > m.distanceSq; }
	};

	// Note: Aligning to the cache line size to prevent cache misses.
	struct alignas(64) MeshBuffer
	{
		IMeshRenderSystem* meshSystem = nullptr;
		atomic<uint32> drawCount = 0;
	};
	struct UnsortedBuffer final : public MeshBuffer
	{
		vector<vector<UnsortedMesh>> threadMeshes;
		vector<UnsortedMesh> combinedMeshes;
	};
	struct SortedBuffer final : MeshBuffer { };
private:
	vector<UnsortedBuffer*> unsortedBuffers;
	vector<SortedBuffer*> sortedBuffers;
	vector<SortedMesh> sortedCombinedMeshes;
	vector<vector<SortedMesh>> sortedThreadMeshes;
	vector<IMeshRenderSystem*> meshSystems;
	uint32 unsortedBufferCount = 0;
	uint32 sortedBufferCount = 0;
	bool hasOIT = false;
	bool asyncRecording = false;
	bool asyncPreparing = false;
	bool hasAnyRefr = false;
	bool hasAnyOIT = false;
	bool hasAnyTransDepth = false;
	atomic<uint32> sortedDrawIndex = 0; // Always last.

	/**
	 * @brief Creates a new mesh rendering system instance.
	 * 
	 * @param useOIT use order independent transparency rendering
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param useAsyncPreparing use multithreaded render meshes preparing
	 * @param setSingleton set system singleton instance
	 */
	MeshRenderSystem(bool useOIT = true, bool useAsyncRecording = true, 
		bool useAsyncPreparing = true, bool setSingleton = true);
	/**
	 * @brief Destroys mesh rendering system instance.
	 */
	~MeshRenderSystem() final;

	void prepareSystems();
	void sortMeshes();
	void prepareMeshes(const f32x4x4& viewProj, f32x4 cameraOffset, uint8 frustumPlaneCount, int8 shadowPass);
	void renderUnsorted(const f32x4x4& viewProj, MeshRenderType renderType, int8 shadowPass);
	void renderSorted(const f32x4x4& viewProj, int8 shadowPass);
	void cleanupMeshes();
	void renderShadows();

	void init();
	void deinit();
	void preForwardRender();
	void forwardRender();
	void preDeferredRender();
	void deferredRender();
	void depthHdrRender();
	void preRefractedRender();
	void refractedRender();
	void translucentRender();
	void preTransDepthRender();
	void transDepthRender();
	void preOitRender();
	void oitRender();

	friend class ecsm::Manager;
	friend class GizmosEditorSystem;
	friend class SelectorEditorSystem;
public:
	bool isOpaqueOnly = false; /** Render only opaque meshes. */

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