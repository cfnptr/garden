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
 * @brief PBR lighting rendering functions. (Physically Based Rendering)
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

class PbrLightingSystem;

/**
 * @brief PBR lighting rendering data container. (Physically Based Rendering)
 */
struct PbrLightingComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< PBR lighting cubemap image. */
	Ref<Buffer> sh = {};                   /**< PBR lighting spherical harmonics buffer. */
	Ref<Image> specular = {};              /**< PBR lighting specular cubemap. */
	Ref<DescriptorSet> descriptorSet = {}; /**< PBR lighting descriptor set. */
private:
	bool dataReady = false;
	friend class PbrLightingSystem;
public:
	/**
	 * @brief Are PBR lighting data transfered and ready.
	 */
	bool isReady() const noexcept { return dataReady; }
};

/**
 * @brief PBR lighting rendering system. (Physically Based Rendering)
 * 
 * @details
 * PBR is a rendering technique designed to simulate how light interacts with surfaces in a realistic manner. It is 
 * based on physical principles, taking into account material properties such as roughness, metallicity, albedo 
 * color, as well as the characteristics of light sources.
 * 
 * Registers events: PreShadowRender, ShadowRender, PostShadowRender ShadowRecreate, 
 * 	                 PreAoRender, AoRender, PostAoRender, AoRecreate, 
 *                   PreReflRender, ReflRender, PostReflRender, ReflRecreate,
 *                   PreGiRender, GiRender, PostGiRender, GiRecreate.
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
		Options() { }
	};

	struct LightingPC final
	{
		float4x4 uvToWorld;
		float4 shadowColor;
		float reflLodOffset;
		float emissiveCoeff;
		float reflectanceCoeff;
		
	};
	struct SpecularPC final
	{
		uint32 imageSize;
		uint32 itemCount;
	};

	static constexpr uint8 accumBufferCount = 3; /**< PBR lighting accumulation buffer count. */
	static constexpr Framebuffer::OutputAttachment::Flags accumBufferFlags = { false, false, true };
	static constexpr Image::Format shadowBufferFormat = Image::Format::UnormR8G8B8A8;
	static constexpr Image::Format aoBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format reflBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format giBufferFormat = Image::Format::SfloatR16G16B16A16;
private:
	ID<Image> dfgLUT = {};
	ID<Buffer> reflKernel = {};
	ID<Image> shadowBuffer = {};
	ID<Image> shadowBlurBuffer = {};
	ID<Image> aoBuffer = {};
	ID<Image> aoBlurBuffer = {};
	ID<Image> reflBuffer = {};
	ID<Image> giBuffer = {};
	ID<Framebuffer> giFramebuffer = {};
	ID<ImageView> shadowImageViews[accumBufferCount] = {};
	ID<Framebuffer> shadowFramebuffers[accumBufferCount + 1] = {};
	ID<ImageView> aoImageViews[accumBufferCount] = {};
	ID<Framebuffer> aoFramebuffers[accumBufferCount] = {};
	vector<ID<ImageView>> reflImageViews;
	vector<ID<Framebuffer>> reflFramebuffers;
	vector<ID<DescriptorSet>> reflBlurDSes;
	ID<ImageView> reflBufferView = {};
	ID<GraphicsPipeline> lightingPipeline = {};
	ID<ComputePipeline> iblSpecularPipeline = {};
	ID<GraphicsPipeline> shadowBlurPipeline = {};
	ID<GraphicsPipeline> aoBlurPipeline = {};
	ID<GraphicsPipeline> reflBlurPipeline = {};
	ID<DescriptorSet> lightingDS = {};
	ID<DescriptorSet> shadowBlurDS = {};
	ID<DescriptorSet> aoBlurDS = {};
	float reflLodOffset = 0.0f;
	Options options;
	bool hasAnyShadow = false;
	bool hasAnyAO = false;
	bool hasAnyRefl = false;
	bool hasAnyGI = false;
	bool isLoaded = false;

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
	 * @param options target system options
	 */
	void setOptions(Options options);

	/**
	 * @brief Returns PBR lighting reflections LOD offset.
	 */
	float getReflLodOffset() const noexcept { return reflLodOffset; }

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
	 * @brief Returns PBR lighting reflection framebuffer array.
	 */
	const vector<ID<Framebuffer>>& getReflFramebuffers();
	/**
	 * @brief Returns PBR lighting global illumination framebuffer.
	 */
	ID<Framebuffer> getGiFramebuffer();

	/**
	 * @brief Returns PBR lighting shadow base framebuffer.
	 */
	ID<Framebuffer> getShadowBaseFB() { return getShadowFramebuffers()[2]; }
	/**
	 * @brief Returns PBR lighting shadow temporary framebuffer.
	 */
	ID<Framebuffer> getShadowTempFB() { return getShadowFramebuffers()[1]; }
	/**
	 * @brief Returns PBR lighting shadow blur framebuffer.
	 */
	ID<Framebuffer> getShadowBlurFB() { return getShadowFramebuffers()[0]; }
	/**
	 * @brief Returns PBR lighting AO base framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoBaseFB() { return getAoFramebuffers()[2]; }
	/**
	 * @brief Returns PBR lighting AO temporary framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoTempFB() { return getAoFramebuffers()[1]; }
	/**
	 * @brief Returns PBR lighting AO blur framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoBlurFB() { return getAoFramebuffers()[0]; }
	/**
	 * @brief Returns PBR lighting reflection base framebuffer.
	 */
	ID<Framebuffer> getReflBaseFB() { return getReflFramebuffers()[0]; }

	/**
	 * @brief Returns PBR lighting DFG LUT image. (DFG Look Up Table)
	 */
	ID<Image> getDfgLUT();
	/**
	 * @brief Returns PBR lighting reflections blur kernel.
	 */
	ID<Buffer> getReflKernel();

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
	 * @brief Returns PBR lighting reflection image view array.
	 */
	const vector<ID<ImageView>>& getReflImageViews();

	/**
	 * @brief Returns PBR lighting shadow base image view.
	 */
	ID<ImageView> getShadowBaseView() { return getShadowImageViews()[2]; }
	/**
	 * @brief Returns PBR lighting shadow temporary image view.
	 */
	ID<ImageView> getShadowTempView() { return getShadowImageViews()[1]; }
	/**
	 * @brief Returns PBR lighting shadow blur image view.
	 */
	ID<ImageView> getShadowBlurView() { return getShadowImageViews()[0]; }
	/**
	 * @brief Returns PBR lighting AO base image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoBaseView() { return getAoImageViews()[2]; }
	/**
	 * @brief Returns PBR lighting AO temporary image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoTempView() { return getAoImageViews()[1]; }
	/**
	 * @brief Returns PBR lighting AO blur image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoBlurView() { return getAoImageViews()[0]; }
	/**
	 * @brief Returns PBR lighting reflection base image view.
	 */
	ID<ImageView> getReflBaseView() { return getReflImageViews()[0]; }

	/*******************************************************************************************************************
	 * @brief Loads cubemap rendering data from the resource pack.
	 * @note Loads from the scenes directory in debug build.
	 * 
	 * @param[in] path target cubemap resource path
	 * @param[out] cubemap cubemap image instance
	 * @param[out] sh spherical harmonics buffer instance
	 * @param[out] specular specular cubemap instance
	 * @param strategy graphics memory allocation strategy
	 * @param[out] shBuffer spherical harmonics data buffer or null
	 */
	void loadCubemap(const fs::path& path, Ref<Image>& cubemap, Ref<Buffer>& sh, Ref<Image>& specular, 
		Memory::Strategy strategy = Memory::Strategy::Size, vector<f32x4>* shBuffer = nullptr);

	/**
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