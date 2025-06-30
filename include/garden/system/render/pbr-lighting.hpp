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

// TODO: I have failed to find good denoiser for shadows. Research this field.

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief PBR lighting rendering data container. (Physically Based Rendering)
 */
struct PbrLightingRenderComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< PBR lighting cubemap image. */
	Ref<Buffer> sh = {};                   /**< PBR lighting spherical harmonics buffer. */
	Ref<Image> specular = {};              /**< PBR lighting specular cubemap. */
	Ref<DescriptorSet> descriptorSet = {}; /**< PBR lighting descriptor set. */
private:
	bool destroy();

	friend class LinearPool<PbrLightingRenderComponent>;
	friend class ComponentSystem<PbrLightingRenderComponent>;
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
 *                   PreReflRender, ReflRender, PostReflRender, ReflRecreate.
 */
class PbrLightingRenderSystem final : public ComponentSystem<PbrLightingRenderComponent>, 
	public Singleton<PbrLightingRenderSystem>
{
public:
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

	static constexpr uint8 shadowBufferCount = 2;  /**< PBR lighting rendering shadow buffer count. */
	static constexpr uint8 aoBufferCount = 3;      /**< PBR lighting rendering AO buffer count. (Ambient Occlusion) */

	static constexpr Image::Format shadowBufferFormat = Image::Format::UnormR8G8B8A8;
	static constexpr Image::Format aoBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format reflBufferFormat = Image::Format::SfloatR16G16B16A16;
private:
	ID<Image> dfgLUT = {};
	ID<Buffer> reflKernel = {};
	ID<Image> shadowBuffer = {};
	ID<Image> aoBuffer = {};
	ID<Image> aoDenBuffer = {};
	ID<Image> reflBuffer = {};
	ID<ImageView> shadowImageViews[shadowBufferCount] = {};
	ID<Framebuffer> shadowFramebuffers[shadowBufferCount + 1] = {};
	ID<ImageView> aoImageViews[aoBufferCount] = {};
	ID<Framebuffer> aoFramebuffers[aoBufferCount] = {};
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
	bool hasShadowBuffer = false;
	bool hasAoBuffer = false;
	bool hasReflBuffer = false;
	bool hasAnyShadow = false;
	bool hasAnyAO = false;
	bool hasAnyRefl = false;

	/**
	 * @brief Creates a new PBR lighting rendering system instance. (Physically Based Rendering)
	 * 
	 * @param useShadowBuffer create and use shadow buffer for rendering
	 * @param useAoBuffer create and use ambient occlusion buffer for rendering
	 * @param useReflBuffer create and use reflection buffer for rendering
	 * @param setSingleton set system singleton instance
	 */
	PbrLightingRenderSystem(bool useShadowBuffer = true, bool useAoBuffer = true, 
		bool useReflBuffer = true, bool setSingleton = true);
	/**
	 * @brief Destroys PBR lighting rendering system instance. (Physically Based Rendering)
	 */
	~PbrLightingRenderSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();

	string_view getComponentName() const final;
	friend class ecsm::Manager;
public:
	float reflectanceCoeff = 1.0f;
	float denoiseSharpness = 50.0f;

	/*******************************************************************************************************************
	 * @brief Use shadow buffer for PBR lighting rendering.
	 */
	bool useShadowBuffer() const noexcept { return hasShadowBuffer; }
	/**
	 * @brief Use ambient occlusion buffer for PBR lighting rendering.
	 */
	bool useAoBuffer() const noexcept { return hasAoBuffer; }
	/**
	 * @brief Use reflection buffer for PBR lighting rendering.
	 */
	bool useReflBuffer() const noexcept { return hasReflBuffer; }
	/**
	 * @brief Enables or disables use of the shadow and ambient occlusion buffers.
	 * @details It destroys existing buffers on use set to false.
	 * 
	 * @param useShadowBuffer use shadow buffer for rendering
	 * @param useAoBuffer use ambient occlusion buffer for rendering
	 * @param useReflBUffer use reflection buffer for rendering
	 */
	void setConsts(bool useShadowBuffer, bool useAoBuffer, bool useReflBuffer);

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
	 * @brief Returns PBR lighting shadow base framebuffer.
	 */
	ID<Framebuffer> getShadowFramebuffer() { return getShadowFramebuffers()[0]; }
	/**
	 * @brief Returns PBR lighting AO base framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoBaseFB() { return getAoFramebuffers()[2]; }
	/**
	 * @brief Returns PBR lighting AO temporary framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoTemporaryFB() { return getAoFramebuffers()[1]; }
	/**
	 * @brief Returns PBR lighting AO denoised framebuffer. (Ambient Occlusion)
	 */
	ID<Framebuffer> getAoDenoisedFB() { return getAoFramebuffers()[0]; }
	/**
	 * @brief Returns PBR lighting reflection base framebuffer.
	 */
	ID<Framebuffer> getReflFramebuffer() { return getReflFramebuffers()[0]; }

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
	 * @brief Returns PBR lighting AO buffer. (Ambient Occlusion)
	 */
	ID<Image> getAoBuffer();
	/**
	 * @brief Returns PBR lighting AO denoise buffer. (Ambient Occlusion)
	 */
	ID<Image> getAoDenBuffer();
	/**
	 * @brief Returns PBR lighting reflection buffer.
	 */
	ID<Image> getReflBuffer();

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
	ID<ImageView> getShadowImageView() { return getShadowImageViews()[0]; }
	/**
	 * @brief Returns PBR lighting shadow denoised image view.
	 */
	ID<ImageView> getShadowDenoisedView() { return getShadowImageViews()[1]; }
	/**
	 * @brief Returns PBR lighting AO base image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoBaseView() { return getAoImageViews()[2]; }
	/**
	 * @brief Returns PBR lighting AO temporary image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoTemporaryView() { return getAoImageViews()[1]; }
	/**
	 * @brief Returns PBR lighting AO denoised image view. (Ambient Occlusion)
	 */
	ID<ImageView> getAoDenoisedView() { return getAoImageViews()[0]; }
	/**
	 * @brief Returns PBR lighting reflection base image view.
	 */
	ID<ImageView> getReflImageView() { return getReflImageViews()[0]; }

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
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param type descriptor set pipeline type
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular, 
		ID<Pipeline> pipeline, PipelineType type, uint8 index);
	/**
	 * @brief Creates PBR lighting graphics descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular, 
		ID<GraphicsPipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(sh, specular, ID<Pipeline>(pipeline), PipelineType::Graphics, index);
	}
	/**
	 * @brief Creates PBR lighting compute descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular, 
		ID<ComputePipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(sh, specular, ID<Pipeline>(pipeline), PipelineType::Compute, index);
	}
	/**
	 * @brief Creates PBR lighting ray tracing descriptor set.
	 * @return A new descriptor set if resources are ready, otherwise null.
	 * 
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 * @param pipeline target descriptor set pipeline ({} = lighting)
	 * @param index index of the descriptor set in the shader
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular, 
		ID<RayTracingPipeline> pipeline, uint8 index = 1)
	{
		return createDescriptorSet(sh, specular, ID<Pipeline>(pipeline), PipelineType::RayTracing, index);
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
	void markAnyRefl() noexcept { hasAnyRefl = true; }
};

} // namespace garden