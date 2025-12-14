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
	UI,          /**< User interface redering. (Uses GUI orthographic projection matrix) */
	Count        /**< Common mesh render type count. */
};

/***********************************************************************************************************************
 * @brief General mesh rendering data container.
 */
struct MeshRenderComponent : public Component
{
protected:
	uint32 reserved0 = 0;
	uint32 reserved1 = 0;
	uint16 reserved2 = 0;
public:
	volatile bool isEnabled = true;  /**< Is mesh should be rendered. */
	volatile bool isVisible = false; /**< Is mesh visible on camera after last frustum culling. */
	Aabb aabb = Aabb::one;           /**< Mesh axis aligned bounding box. */
};

/**
 * @brief Mesh render system interface.
 */
class IMeshRenderSystem
{
public:
	using MeshRenderPool = LinearPool<MeshRenderComponent, false>;
protected:
	/**
	 * @brief Is mesh system ready for rendering. (All resources loaded, etc.)
	 * @param shadowPass shadow pass index (light pass = -1)
	 */
	virtual bool isDrawReady(int8 shadowPass) = 0;
	/**
	 * @brief Prepares data required for mesh rendering.
	 *
	 * @param[in] viewProj camera view * projection matrix
	 * @param drawCount system mesh count to draw
	 * @param instanceCount system mesh count to draw (>= drawCount)
	 * @param shadowPass shadow pass index (light pass = -1)
	 */
	virtual void prepareDraw(const f32x4x4& viewProj, uint32 drawCount, uint32 instanceCount, int8 shadowPass) { }
	/**
	 * @brief Begins mesh drawing asynchronously.
	 * @warning This function is called asynchronously from the thread pool!
	 * @param taskIndex task index in the thread pool
	 */
	virtual void beginDrawAsync(int32 taskIndex) { }
	/**
	 * @brief Returns mesh instance count to draw.
	 * @param meshRenderView target mesh render view
	 */
	virtual uint32 getInstancesAsync(MeshRenderComponent* meshRenderView) const { return 1; }
	/**
	 * @brief Draws mesh instances asynchronously.
	 * @warning This function is called asynchronously from the thread pool!
	 * 
	 * @param[in,out] meshRenderView target mesh render view
	 * @param[in] viewProj camera view * projection matrix
	 * @param[in] model mesh model matrix (position, scale, rotation, etc.)
	 * @param instanceIndex mesh instance draw index (sorted)
	 * @param taskIndex task index in the thread pool
	 */
	virtual void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 instanceIndex, int32 taskIndex) = 0;
	/**
	 * @brief Ends mesh drawing asynchronously.
	 * @warning This function is called asynchronously from the thread pool!
	 * @param instanceCount task drawn mesh instance count
	 */
	virtual void endDrawAsync(uint32 instanceCount, int32 taskIndex) { }
	/**
	 * @brief Finalizes data used for mesh rendering. (Flush buffers, etc.)
	 * @param instanceCount total drawn mesh instance count
	 */
	virtual void finalizeDraw(uint32 instanceCount) { }
	/**
	 * @brief Cleans up data used for all mesh rendering. (Flush buffers, etc.)
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
	virtual MeshRenderPool& getMeshComponentPool() const = 0;
	/**
	 * @brief Returns system mesh component size in bytes.
	 */
	virtual psize getMeshComponentSize() const = 0;

	/**
	 * @brief Returns ready for rendering mesh instance count. (If mesh resources loaded)
	 * @warning This function is called asynchronously from the thread pool!
	 *
	 * @param meshRenderView target mesh render view
	 * @param[in] cameraPosition camera world position for model matrix
	 * @param[in] frustum camera frustum planes for mesh culling
	 * @param[in,out] model mesh model matrix (position, scale, rotation, etc.)
	 */
	virtual uint32 getReadyMeshesAsync(MeshRenderComponent* meshRenderView, 
		const f32x4& cameraPosition, const Frustum& frustum, f32x4x4& model)
	{
		return isBehindFrustum(frustum, meshRenderView->aabb, model) ? 0 : 1;
	}
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
	struct UnsortedMesh final
	{
		psize componentOffset = 0;
		float4x3 bakedModel = float4x3::zero;
		float distanceSq = 0.0f;
		bool operator<(const UnsortedMesh& m) const noexcept { return distanceSq < m.distanceSq; }
	};
	struct SortedMesh final
	{
		psize componentOffset = 0;
		float4x3 bakedModel = float4x3::zero;
		float distanceSq = 0.0f;
		uint32 bufferIndex = 0;
		bool operator<(const SortedMesh& m) const noexcept { return distanceSq > m.distanceSq; }
	};

	struct MeshBuffer
	{
		IMeshRenderSystem* meshSystem = nullptr;
		atomic<uint32> drawCount = 0;
		alignas(std::hardware_destructive_interference_size) atomic<uint32> instanceCount = 0;
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
	vector<SortedMesh> transSortedMeshes;
	vector<SortedMesh> uiSortedMeshes;
	vector<vector<SortedMesh>> sortedThreadMeshes;
	vector<IMeshRenderSystem*> meshSystems;
	atomic<uint32> transDrawIndex = 0;
	uint32 unsortedBufferCount = 0;
	uint32 sortedBufferCount = 0;
	bool hasOIT = false;
	bool asyncRecording = false;
	bool asyncPreparing = false;
	bool hasAnyRefr = false;
	bool hasAnyOIT = false;
	bool hasAnyTransDepth = false;
	alignas(std::hardware_destructive_interference_size) atomic<uint32> uiDrawIndex = 0;

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
	void prepareMeshes(const Frustum& viewFrustum, const Frustum* uiFrustum, f32x4 cameraOffset, int8 shadowPass);
	void renderUnsorted(const f32x4x4& viewProj, MeshRenderType renderType, int8 shadowPass);
	void renderSorted(const f32x4x4& viewProj, MeshRenderType renderType, int8 shadowPass);
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
	void uiRender();

	friend class ecsm::Manager;
	friend class GizmosEditorSystem;
	friend class SelectorEditorSystem;
public:
	bool isNonTranslucent = false; /** Render only non translucent meshes. */

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