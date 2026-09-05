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
 * @brief 3D model rendering functions.
 */

#pragma once
#include "garden/system/render/instance.hpp"

namespace garden
{

class ModelRenderSystem;

/**
 * @brief 3D model file container types.
 */
enum class ModelFileType : uint8
{
	USD, glTF, FBX, OBJ, Count
};

/**
 * @brief 3D model LOD rendering data container. (Levels of detail)
 */
struct ModelLOD
{
	Ref<Buffer> vertexBuffer = {}; /**< Buffer containing 3D model vertex data. */
	Ref<Buffer> indexBuffer = {};  /**< Buffer containing 3D model indices. */

	static constexpr uint8 maxCount = UINT8_MAX; /**< Maximal 3D model lod count. */
};
/**
 * @brief General 3D model rendering data container.
 */
struct ModelRenderComponent : public MeshRenderComponent
{
protected:
	float3x4 lastModel = float3x4::identity;
	ModelLOD* lods = nullptr;

	uint32& _colorMapID() noexcept { return reserved0; }
	uint32& _normalMapID() noexcept { return reserved1; }
	uint32& _ormMapID() noexcept { return ormMapID; }

	void _setLodCount(uint8 value) noexcept { reserved2 = (reserved2 & 0xFF00u) | value; }
	void _setLodCapacity(uint8 value) noexcept { reserved2 = (reserved2 & 0xFFu) | (value << 8u); }

	friend class ModelRenderSystem;
public:
	Ref<Image> colorMap = {};            /**< Color map texture instance. */
	Ref<Image> normalMap = {};            /**< Color map texture instance. */
	Ref<Image> ormMap = {};              /**< ORM map texture instance. */
	half2 uvSize = half2::one;           /**< Texture UV size. */
	half2 uvOffset = half2::zero;        /**< Texture UV offset. */
	Color colorAdd = Color::transparent; /**< Added to the color map. */
	Color colorMul = Color::white;       /**< Multiplied by the color map. */
	Color ormAdd = Color::transparent;   /**< Added to the ORM map. */
	Color ormMul = Color::white;         /**< Multiplied by the ORM map. */
	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = "";          /**< Color map texture path. */
	fs::path normalMapPath = "";         /**< Normal map texture path. */
	fs::path ormMapPath = "";            /**< ORM map texture path. */
	#endif
protected:
	uint32 ormMapID = 0;
public:
	/**
	 * @brief Returns 3D model LOD array. (Levels of detail)
	 */
	ModelLOD* getLods() noexcept { return lods; }
	/**
	 * @brief Returns 3D model LOD array. (Levels of detail)
	 */
	const ModelLOD* getLods() const noexcept { return lods; }

	/**
	 * @brief Returns 3D model LOD array size.
	 */
	uint8 getLodCount() const noexcept { return reserved2 & 0xFFu; }
	/**
	 * @brief Returns 3D model LOD array capacity.
	 */
	uint8 getLodCapacity() const noexcept { return (reserved2 >> 8u) & 0xFFu; }

	/**
	 * @brief Returns 3D model LOD at specified index. (Level of detail)
	 * @param index target level of detail index
	 */
	ModelLOD& getLod(uint8 index) noexcept
	{
		GARDEN_ASSERT(index < getLodCount());
		return lods[index];
	}
	/**
	 * @brief Returns 3D model LOD at specified index. (Level of detail)
	 * @param index target level of detail index
	 */
	const ModelLOD& getLod(uint8 index) const noexcept
	{
		GARDEN_ASSERT(index < getLodCount());
		return lods[index];
	}

	/**
	 * @brief Sets 3D model LOD array size.
	 * @param count target level of detail count
	 */
	void setLodCount(uint8 count);

	/**
	 * @brief Adds a new 3D model level of detail.
	 * @param lod target level of detail to add
	 */
	void addLod(const ModelLOD& lod = {})
	{
		auto lodIndex = getLodCount();
		setLodCount(lodIndex + 1);
		getLod(lodIndex) = lod;
	}
	/**
	 * @brief Removes 3D model level of detail.
	 */
	void removeLod()
	{
		auto lodCount = getLodCount();
		GARDEN_ASSERT(lodCount > 0);
		setLodCount(lodCount - 1);
	}
};

/**
 * @brief General 3D model animation frame container.
 */
struct ModelAnimFrame : public AnimationFrame
{
	uint8 animateIsEnabled : 1;
	uint8 animateUvSize : 1;
	uint8 animateUvOffset : 1;
	uint8 animateColorAdd : 1;
	uint8 animateColorMul : 1;
	uint8 animateOrmAdd : 1;
	uint8 animateOrmMul : 1;
	uint8 isEnabled : 1;
protected:
	uint16 _alignment0 = 0;
public:
	half2 uvSize = half2::one;
	half2 uvOffset = half2::zero;
	Color colorAdd = Color::transparent;
	Color colorMul = Color::white;
	Color ormAdd = Color::transparent;
	Color ormMul = Color::white;

	ModelAnimFrame() noexcept : animateIsEnabled(false), animateUvSize(false), animateUvOffset(false),
		animateColorAdd(false), animateColorMul(false), animateOrmAdd(false), animateOrmMul(false), isEnabled(true) { }

	bool hasAnimation() override
	{
		return animateIsEnabled | animateUvSize | animateUvOffset | 
			animateColorAdd | animateColorMul | animateOrmAdd | animateOrmMul;
	}
};

/***********************************************************************************************************************
 * @brief 3D model rendering system.
 */
class ModelRenderSystem : public InstanceRenderSystem, public ISerializable
{
public:
	struct PushConstants
	{
		uint32 instanceIndex;
	};

	static const vector<string_view> modelFileExts;    /**< Supported model file extensions. */
	static const vector<ModelFileType> modelFileTypes; /**< Supported model file types. */

	using ModelFramePool = LinearPool<ModelAnimFrame>;
protected:
	fs::path pipelinePath = "";
	string valueStringCache;

	/**
	 * @brief Creates a new 3D model rendering system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	ModelRenderSystem(const fs::path& pipelinePath) : pipelinePath(pipelinePath) { }

	void init() override;
	virtual void imageLoaded();
	virtual void fileDrop();

	static void resetComponent(View<Component> component);

	uint32 getReadyMeshesAsync(MeshRenderComponent* meshRenderView, 
		const f32x4& cameraPosition, const Frustum& frustum, f32x4x4& model) override;
	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 instanceIndex, int32 taskIndex) override;

	uint64 getBaseInstanceDataSize() override;
	virtual void setInstanceData(ModelRenderComponent* modelRenderView, void* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex);
	virtual void setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex);
	virtual DescriptorSet::Uniforms getModelUniforms(ID<ImageView> colorMap);
	ID<GraphicsPipeline> createBasePipeline() override;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, View<Component> component) override;

	static void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame);
	static void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame);
	static void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t);

	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns 3D model system animation frame pool.
	 */
	virtual ModelFramePool& getModelFramePool() = 0;
	/**
	 * @brief Returns 3D model system animation frame size in bytes.
	 */
	virtual psize getModelFrameSize() const = 0;

	#if GARDEN_DEBUG || GARDEN_EDITOR || defined(GARDEN_MODEL_CONVERTER)
	/**
	 * @brief Loads 3D model from the specified file.
	 * @throw GardenError on 3D model data loading error.
	 * 
	 * @param[in] path target 3d model file path
	 * @param[in] components model rendering component types or null
	 */
	static ID<Entity> loadModel(const fs::path& path, const map<string, type_index>* components = nullptr);
	#endif
};

/***********************************************************************************************************************
 * @brief Base 3d model mesh rendering system with components and animation frames.
 * @details See the @ref ModelRenderSystem.
 *
 * @tparam C type of the system component
 * @tparam F type of the system animation frame
 *
 * @tparam DestroyComponents system should call destroy() function of the components
 * @tparam DestroyAnimationFrames system should call destroy() function of the animation frames
 */
template<class C = Component, class F = AnimationFrame, 
	bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class ModelCompAnimSystem : public CompAnimSystem<C, F, 
	DestroyComponents, DestroyAnimationFrames>, public ModelRenderSystem
{
protected:
	/**
	 * @brief Creates a new 3d model mesh render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	ModelCompAnimSystem(const fs::path& pipelinePath) : ModelRenderSystem(pipelinePath) { }

	void resetComponent(View<Component> component, bool full) override
	{ ModelRenderSystem::resetComponent(component); if (full) **View<C>(component) = C(); }

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override
	{ ModelRenderSystem::serializeAnimation(serializer, frame); }
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) override
	{ ModelRenderSystem::deserializeAnimation(deserializer, frame); }
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) override
	{ ModelRenderSystem::animateAsync(component, a, b, t); }

	MeshRenderPool& getMeshComponentPool() const override { return *((MeshRenderPool*)&this->components); }
	psize getMeshComponentSize() const override { return sizeof(C); }
	ModelFramePool& getModelFramePool() override { return *((ModelFramePool*)&this->animationFrames); }
	psize getModelFrameSize() const override { return sizeof(F); }
};

} // namespace garden