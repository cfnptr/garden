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
 * @brief PBR lighting rendering functions. (Physically Based Rendering)
 */

#pragma once
#include "garden/system/graphics.hpp"
#include "math/ibl.hpp"

namespace garden
{

class PbrLightingSystem;

/**
 * @brief PBR lighting cubemap rendering modes.
 */
enum class PbrCubemapMode
{
	Static,  /**< Cubemap is loaded once and reused for rendering. (Skybox) */
	Dynamic, /**< Cubemap is rendered and updated at runtime. (Atmosphere) */
	Count    /**< PBR lighting cubemap rendering mode count. */
};

/**
 * @brief PBR lighting rendering data container. (Physically Based Rendering)
 *
 * @details
 * Physically Based Rendering is a shading and rendering approach that seeks to simulate how light interacts with 
 * surfaces in a way that accurately mimics the real world. Unlike legacy "ad-hoc" lighting models that relied on 
 * manual artistic approximations, PBR uses mathematically based formulas to ensure that materials conserve energy 
 * and react realistically under varying lighting conditions. By utilizing standardized parameters such as 
 * Albedo (base color), Roughness (surface microsurface detail), and Metallicity (conductive properties), PBR ensures 
 * that assets look consistent and "correct" across different environments, from a dark dungeon to a sunlit field, 
 * without requiring per-scene manual adjustments.
 */
struct PbrLightingComponent final : public Component
{
	Ref<Image> skybox = {};                /**< Skybox cubemap image. */
	Ref<Buffer> shBuffer = {};             /**< Spherical harmonics buffer. */
	Ref<Image> specular = {};              /**< Specular cubemap image. */
	Ref<DescriptorSet> descriptorSet = {}; /**< PBR lighting descriptor set. */
	f32x4 shCoeffs[ibl::shCoeffCount];     /**< Spherical harmonics coefficients. */
private:
	PbrCubemapMode mode = PbrCubemapMode::Static;
	friend class PbrLightingSystem;
public:
	/**
	 * @brief Returns PBR lighting cubemap rendering mode.
	 */
	PbrCubemapMode getCubemapMode() const noexcept { return mode; }
	/**
	 * @brief Sets PBR lighting cubemap rendering mode.
	 * @param mode target PBR cubemap mode
	 */
	void setCubemapMode(PbrCubemapMode mode);
};

/**
 * @brief PBR lighting rendering system. (Physically Based Rendering)
 * 
 * @details
 * PBR is a rendering technique designed to simulate how light interacts with surfaces in a realistic manner. It is 
 * based on physical principles, taking into account material properties such as roughness, metallicity, albedo 
 * color, as well as the characteristics of light sources.
 * 
 * Registers events:
 *   PreShadowRender, ShadowRender, PostShadowRender ShadowRecreate, 
 * 	 PreAoRender, AoRender, PostAoRender, AoRecreate, 
 *   PreReflRender, ReflRender, PostReflRender, ReflRecreate,
 *   PreGiRender, GiRender, PostGiRender, GiRecreate.
 */
class PbrLightingSystem final : public ComponentSystem<
	PbrLightingComponent, false>, public Singleton<PbrLightingSystem>
{
public:
	/**
	 * @brief PBR lighting rendering system initialization options.
	 */
	struct Options final
	{
		bool useShadowBuffer = true; /**< Create and use shadow buffer for rendering. */
		bool useAoBuffer = true;     /**< Create and use ambient occlusion buffer for rendering. */
		bool useReflBuffer = true;   /**< Create and use reflection buffer for rendering. */
		bool useGiBuffer = true;     /**< Create and use global illumination buffer for rendering. */
		bool useReflBlur = true;     /**< Create and use reflection buffer blur chain. */
		Options() noexcept { }
	};

	struct LightingPC final
	{
		float4x4 uvToWorld;
		float4 shadowColor;
		float emissiveCoeff;
		float reflectanceCoeff;
		float ggxLodOffset;
	};
	struct SpecularPC final
	{
		uint32 imageSize;
		uint32 sampleOffset;
		uint32 sampleCount;
		uint32 faceOffset;
		float weight;
	};

	static constexpr uint8 baseShadowIndex = 0;
	static constexpr uint8 tempShadowIndex = 1;
	static constexpr uint8 blurShadowIndex = 0;
	static constexpr uint8 shadowBufferCount = 2;
	static constexpr uint8 baseAoIndex = 2;
	static constexpr uint8 tempAoIndex = 1;
	static constexpr uint8 blurAoIndex = 0;
	static constexpr uint8 aoBufferCount = 3;
	static constexpr uint8 baseReflIndex = 0;
	static constexpr uint8 baseGiIndex = 0;

	static constexpr Framebuffer::OutputAttachment::Flags baseFbFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags tempFbFlags = { false, false, true };

	static constexpr Image::Format shadowBufferFormat = Image::Format::UnormR8G8B8A8;
	static constexpr Image::Format aoBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format reflBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format giBufferFormat = Image::Format::SfloatR16G16B16A16;
private:
	vector<ID<ImageView>> reflImageViews;
	vector<ID<Framebuffer>> reflFramebuffers;
	vector<ID<DescriptorSet>> reflBlurDSes;
	ID<Image> dfgLUT = {};
	ID<Image> shadowBuffer = {};
	ID<Image> shadowBlurBuffer = {};
	ID<Image> aoBuffer = {};
	ID<Image> aoBlurBuffer = {};
	ID<Image> reflBuffer = {};
	ID<Image> giBuffer = {};
	ID<Framebuffer> reflFramebuffer = {}, giFramebuffer = {};
	ID<ImageView> shadowImageViews[shadowBufferCount] = {};
	ID<Framebuffer> shadowFramebuffers[shadowBufferCount + 1] = {};
	ID<ImageView> aoImageViews[aoBufferCount] = {};
	ID<Framebuffer> aoFramebuffers[aoBufferCount] = {};
	ID<GraphicsPipeline> lightingPipeline = {};
	ID<ComputePipeline> iblSpecularPipeline = {};
	ID<GraphicsPipeline> shadowBlurPipeline = {};
	ID<GraphicsPipeline> aoBlurPipeline = {};
	ID<GraphicsPipeline> reflBlurPipeline = {};
	ID<DescriptorSet> lightingDS = {};
	ID<DescriptorSet> shadowBlurDS = {};
	ID<DescriptorSet> aoBlurDS = {};
	Options options = {};
	GraphicsQuality quality = GraphicsQuality::High;
	bool hasFbShadow = false;
	bool hasAnyShadow = false;
	bool hasAnyAO = false;
	bool hasAnyRefl = false;
	bool hasAnyGI = false;
	uint8 _alignment = 0;

	/**
	 * @brief Creates a new PBR lighting rendering system instance. (Physically Based Rendering)
	 * 
	 * @param options target system initialization options
	 * @param setSingleton set system singleton instance
	 */
	PbrLightingSystem(Options options = {}, bool setSingleton = true);
	/**
	 * @brief Destroys PBR lighting rendering system instance. (Physically Based Rendering)
	 */
	~PbrLightingSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();
	void qualityChange();

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;

	friend class ecsm::Manager;
public:
	float reflectanceCoeff = 1.0f;
	float blurSharpness = 50.0f;

	/*******************************************************************************************************************
	 * @brief Returns PBR lighting rendering system options.
	 */
	Options getOptions() const noexcept { return options; }
	/**
	 * @brief Enables or disables use of the specific system rendering options.
	 * @details It destroys existing buffers on use set to false.
	 * @param options target PBR lighting system options
	 */
	void setOptions(Options options);

	/**
	 * @brief Returns PBR lighting rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Sets PBR lighting rendering graphics quality.
	 * @param quality target graphics quality level
	 */
	void setQuality(GraphicsQuality quality);

	/**
	 * @brief Returns PBR lighting graphics pipeline.
	 */
	ID<GraphicsPipeline> getLightingPipeline();
	/**
	 * @brief Returns PBR lighting IBL specular graphics pipeline. (Image Based Lighting)
	 */
	ID<ComputePipeline> getIblSpecularPipeline();

	/**
	 * @brief Returns PBR lighting shadow framebuffer array.
	 */
	const ID<Framebuffer>* getShadowFramebuffers();
	/**
	 * @brief Returns PBR lighting AO framebuffer array. (Ambient Occlusion)
	 */
	const ID<Framebuffer>* getAoFramebuffers();
	/**
	 * @brief Returns PBR lighting blur reflection framebuffer array.
	 */
	const vector<ID<Framebuffer>>& getReflFramebuffers();
	/**
	 * @brief Returns PBR lighting reflection framebuffer.
	 */
	ID<Framebuffer> getReflFramebuffer();
	/**
	 * @brief Returns PBR lighting global illumination framebuffer.
	 */
	ID<Framebuffer> getGiFramebuffer();

	/**
	 * @brief Returns PBR lighting shadow base framebuffer.
	 */
	ID<Framebuffer> getShadowBaseFB() { return getShadowFramebuffers()[baseShadowIndex]; }
	/**
	 * @brief Returns PBR lighting shadow temporary framebuffer.
	 */
	ID<Framebuffer> getShadowTempFB() { return getShadowFramebuffers()[tempShadowIndex]; }
	/**
	 * @brief Returns PBR lighting shadow blur framebuffer.
	 */
	ID<Framebuffer> getShadowBlurFB() { return getShadowFramebuffers()[blurShadowIndex]; }
	/**
	 * @brief Returns PBR lighting AO base framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoBaseFB() { return getAoFramebuffers()[baseAoIndex]; }
	/**
	 * @brief Returns PBR lighting AO temporary framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoTempFB() { return getAoFramebuffers()[tempAoIndex]; }
	/**
	 * @brief Returns PBR lighting AO blur framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoBlurFB() { return getAoFramebuffers()[blurAoIndex]; }
	/**
	 * @brief Returns PBR lighting AO blur framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getReflBaseFB()
	{
		return options.useReflBlur ? getReflFramebuffers()[baseReflIndex] : getReflFramebuffer();
	}

	/*******************************************************************************************************************
	 * @brief Returns PBR lighting DFG LUT image. (DFG Look Up Table)
	 */
	ID<Image> getDfgLUT();
	/**
	 * @brief Returns PBR lighting spherical GGX distribution blur kernel.
	 */
	ID<Buffer> getGgxKernel();

	/**
	 * @brief Returns PBR lighting shadow buffer.
	 */
	ID<Image> getShadowBuffer();
	/**
	 * @brief Returns PBR lighting shadow buffer.
	 */
	ID<Image> getShadBlurBuffer();
	/**
	 * @brief Returns PBR lighting AO buffer. (Ambient Occlusion)
	 */
	ID<Image> getAoBuffer();
	/**
	 * @brief Returns PBR lighting AO blur buffer. (Ambient Occlusion)
	 */
	ID<Image> getAoBlurBuffer();
	/**
	 * @brief Returns PBR lighting reflection buffer.
	 */
	ID<Image> getReflBuffer();
	/**
	 * @brief Returns PBR lighting global illumination buffer.
	 */
	ID<Image> getGiBuffer();

	/**
	 * @brief Returns PBR lighting shadow image view array.
	 */
	const ID<ImageView>* getShadowImageViews();
	/**
	 * @brief Returns PBR lighting AO image view array. (Ambient Occlusion)
	 */
	const ID<ImageView>* getAoImageViews();
	/**
	 * @brief Returns PBR lighting blur reflection image view array.
	 */
	const vector<ID<ImageView>>& getReflImageViews();

	/**
	 * @brief Returns PBR lighting shadow base image view.
	 */
	ID<ImageView> getShadowBaseView() { return getShadowImageViews()[baseShadowIndex]; }
	/**
	 * @brief Returns PBR lighting shadow temporary image view.
	 */
	ID<ImageView> getShadowTempView() { return getShadowImageViews()[tempShadowIndex]; }
	/**
	 * @brief Returns PBR lighting shadow temporary image view.
	 */
	ID<ImageView> getShadowBlurView() { return getShadowImageViews()[blurShadowIndex]; }
	/**
	 * @brief Returns PBR lighting AO base image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoBaseView() { return getAoImageViews()[baseAoIndex]; }
	/**
	 * @brief Returns PBR lighting AO temporary image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoTempView() { return getAoImageViews()[tempAoIndex]; }
	/**
	 * @brief Returns PBR lighting AO blur image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoBlurView() { return getAoImageViews()[blurAoIndex]; }
	/**
	 * @brief Returns PBR lighting reflection base image view.
	 */
	ID<ImageView> getReflBaseView()
	{
		return options.useReflBlur ? getReflImageViews()[0] : 
			GraphicsSystem::Instance::get()->get(reflBuffer)->getDefaultView();
	}

	/*******************************************************************************************************************
	 * @brief Calculates specular cubemap mip level count.
	 * @param cubemapSize cubemap size along one axis in pixels
	 */
	static uint8 calcSpecularMipCount(uint32 cubemapSize) noexcept
	{
		constexpr uint8 maxMipCount = 5; // Note: Optimal value based on filament research.
		return std::min(calcMipCount(cubemapSize), maxMipCount);
	}

	/**
	 * @brief Creates IBL specular cubemap cache buffer. (Image Based Lighting)
	 * 
	 * @param cubemapSize cubemap size along one axis in pixels
	 * @param[out] iblWeightBuffer sample weight per mip level
	 * @param[out] iblCountBuffer sample count per mip level
	 */
	ID<Buffer> createSpecularCache(uint32 cubemapSize, vector<float>& iblWeightBuffer, 
		vector<uint32>& iblCountBuffer, Buffer::Usage usage = Buffer::Usage::Storage | Buffer::Usage::TransferDst);
	/**
	 * @brief Creates IBL specular image views. (Image Based Lighting)
	 * 
	 * @param specular target specular cubemap instance
	 * @param[out] specularViews specular image view array
	 */
	void createIblSpecularViews(ID<Image> specular, vector<ID<ImageView>>& specularViews);
	/**
	 * @brief Creates IBL specular descriptor sets. (Image Based Lighting)
	 * 
	 * @param skybox target skybox cubemap instance
	 * @param[in] specularCache specular cache buffer
	 * @param[in] specularViews specular image view array
	 * @param[out] descriptorSets specular descriptor set array
	 */
	void createIblDescriptorSets(ID<Image> skybox, ID<Buffer> specularCache, 
		const vector<ID<ImageView>>& specularViews, vector<ID<DescriptorSet>>& descriptorSets);

	/**
	 * @brief Dispatches IBL specular calculation command. (Image Based Lighting)
	 * 
	 * @param skybox target skybox cubemap instance
	 * @param specular specular cubemap instance
	 * @param[in] iblWeightBuffer sample weight per mip level
	 * @param[in] iblCountBuffer sample count per mip level
	 * @param[in] iblDescriptorSets IBL specular descriptor set array
	 * @param face cubemap face index to dispatch (-1 = all faces)
	 */
	void dispatchIblSpecular(ID<Image> skybox, ID<Image> specular, const vector<float>& iblWeightBuffer,
		const vector<uint32>& iblCountBuffer, const vector<ID<DescriptorSet>>& iblDescriptorSets, int8 face = -1);

	/**
	 * @brief Process IBL spherical harmonics global illumination. (Image Based Lighting)
	 * @param[in,out] shBuffer spherical harmonics coefficients buffer (SH)
	 */
	static void processIblSH(f32x4* shBuffer) noexcept;
	/**
	 * @brief Generates IBL spherical harmonics global illumination. (Image Based Lighting)
	 * 
	 * @param[in] skyboxFaces skybox face array
	 * @param skyboxSize skybox face size along one axis in pixels
	 * @param[out] shBuffer spherical harmonics coefficients buffer (SH)
	 */
	static void generateIblSH(const float4* const* skyboxFaces, uint32 skyboxSize, vector<f32x4>& shBuffer);

	/**
	 * @brief Loads cubemap rendering data from the resource pack.
	 * @note Loads from the scenes directory in debug build.
	 * 
	 * @param[in] path target cubemap resource path
	 * @param format required image data format
	 * @param[out] cubemap cubemap image instance
	 * @param[out] sh spherical harmonics buffer instance
	 * @param[out] specular specular cubemap instance
	 * @param strategy graphics memory allocation strategy
	 * @param[out] shBuffer spherical harmonics data buffer or null
	 */
	void loadCubemap(const fs::path& path, Image::Format format, Ref<Image>& cubemap, Ref<Buffer>& sh, 
		Ref<Image>& specular, Memory::Strategy strategy = Memory::Strategy::Size, vector<f32x4>* shBuffer = nullptr);

	/*******************************************************************************************************************
	 * @brief Creates PBR lighting descriptor set.
	 *
	 * @param entity target PBR lighting entity
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param type descriptor set pipeline type
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Entity> entity, 
		ID<Pipeline> pipeline, PipelineType type, uint8 index);
	/**
	 * @brief Creates PBR lighting graphics descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param entity target PBR lighting entity
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Entity> entity, ID<GraphicsPipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(entity, ID<Pipeline>(pipeline), PipelineType::Graphics, index);
	}
	/**
	 * @brief Creates PBR lighting compute descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param entity target PBR lighting entity
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Entity> entity, ID<ComputePipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(entity, ID<Pipeline>(pipeline), PipelineType::Compute, index);
	}
	/**
	 * @brief Creates PBR lighting ray tracing descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param entity target PBR lighting entity
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Entity> entity, ID<RayTracingPipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(entity, ID<Pipeline>(pipeline), PipelineType::RayTracing, index);
	}

	/**
	 * @brief Returns true if there is rendered framebuffer shadow data on the current frame.
	 */
	bool isFbShadow() const noexcept { return hasFbShadow; }

	/**
	 * @brief Marks that there is rendered framebuffer shadow data on the current frame.
	 * @details See the @ref useShadowBuffer().
	 */
	void markFbShadow() noexcept { hasFbShadow = true; }
	/**
	 * @brief Marks that there is rendered shadow data on the current frame.
	 * @details See the @ref useShadowBuffer().
	 */
	void markAnyShadow() noexcept { hasAnyShadow = true; }
	/**
	 * @brief Marks that there is rendered AO data on the current frame.
	 * @details See the @ref useAoBuffer().
	 */
	void markAnyAO() noexcept { hasAnyAO = true; }
	/**
	 * @brief Marks that there is rendered reflection data on the current frame.
	 * @details See the @ref useReflBuffer().
	 */
	void markAnyReflection() noexcept { hasAnyRefl = true; }
	/**
	 * @brief Marks that there is rendered global illumination data on the current frame.
	 * @details See the @ref useGiBuffer().
	 */
	void markAnyGI() noexcept { hasAnyGI = true; }
};

} // namespace garden