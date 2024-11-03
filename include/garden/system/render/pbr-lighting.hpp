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
 * @brief PBR lighting rendering functions. (Physically Based Rendering)
 */

// TODO: I have failed to find good denoiser for shadows. Research this field.

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

// TODO: TODO: check if correct.
//float iorToReflectance(float transmittedIor, float incidentIor = 1.0)
//{
// float f0 = sqrt((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
// return sqrt(f0 / 0.16);
//}

/**
 * @brief PBR lighting rendering data container. (Physically Based Rendering)
 */
struct PbrLightingRenderComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< PBR lighting cubemap image. */
	Ref<Buffer> sh = {};                   /**< PBR lighting spherical harmonics buffer. */
	Ref<Image> specular = {};              /**< PBR lighting specular cubemap. */
	Ref<DescriptorSet> descriptorSet = {}; /**< PBR lighting descriptor set. */

	bool destroy();
};

/**
 * @brief Shadow rendering system interface.
 */
class IShadowRenderSystem
{
protected:
	/**
	 * @brief Prepares system for shadow rendering.
	 */
	virtual void preShadowRender() { }
	/**
	 * @brief Renders system shadows.
	 */
	virtual bool shadowRender() = 0;

	friend class PbrLightingRenderSystem;
};
/**
 * @brief Ambient occlusion rendering system interface.
 */
class IAoRenderSystem
{
protected:
	/**
	 * @brief Prepares system for ambient occlusion rendering.
	 */
	virtual void preAoRender() { }
	/**
	 * @brief Renders system ambient occlusion.
	 */
	virtual bool aoRender() = 0;

	friend class PbrLightingRenderSystem;
};

/***********************************************************************************************************************
 * @brief PBR lighting rendering system. (Physically Based Rendering)
 */
class PbrLightingRenderSystem final : public ComponentSystem<PbrLightingRenderComponent>, 
	public Singleton<PbrLightingRenderSystem>
{
public:
	struct LightingPC final
	{
		float4x4 uvToWorld;
		float4 shadowColor;
	};
	struct SpecularPC final
	{
		uint32 count;
	};

	/**
	 * @brief PBR lighting rendering shadow buffer count.
	 */
	static constexpr uint8 shadowBufferCount = 1;
	/**
	 * @brief PBR lighting rendering AO buffer count. (Ambient Occlusion)
	 */
	static constexpr uint8 aoBufferCount = 2;
private:
	vector<IShadowRenderSystem*> shadowSystems;
	vector<IAoRenderSystem*> aoSystems;
	ID<Image> dfgLUT = {};
	ID<Image> shadowBuffer = {};
	ID<Image> aoBuffer = {};
	ID<ImageView> shadowImageViews[shadowBufferCount] = {};
	ID<ImageView> aoImageViews[aoBufferCount] = {};
	ID<Framebuffer> shadowFramebuffers[shadowBufferCount] = {};
	ID<Framebuffer> aoFramebuffers[aoBufferCount] = {};
	ID<GraphicsPipeline> lightingPipeline = {};
	ID<ComputePipeline> iblSpecularPipeline = {};
	ID<GraphicsPipeline> aoDenoisePipeline = {};
	ID<DescriptorSet> lightingDescriptorSet = {};
	ID<DescriptorSet> aoDenoiseDescriptorSet = {};
	bool hasShadowBuffer = false;
	bool hasAoBuffer = false;
	uint16 _alignment = 0;

	/**
	 * @brief Creates a new PBR lighting rendering system instance. (Physically Based Rendering)
	 * 
	 * @param useShadowBuffer create and use shadow buffer for rendering
	 * @param useAoBuffer create and use ambient occlusion buffer for rendering
	 * @param setSingleton set system singleton instance
	 */
	PbrLightingRenderSystem(bool useShadowBuffer = false, bool useAoBuffer = false, bool setSingleton = true);
	/**
	 * @brief Destroys PBR lighting rendering system instance. (Physically Based Rendering)
	 */
	~PbrLightingRenderSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();

	const string& getComponentName() const final;
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Shadow color factor. (RGBA)
	 */
	float4 shadowColor = float4(1.0f);

	/**
	 * @brief Use shadow buffer for PBR lighting rendering.
	 */
	bool useShadowBuffer() const noexcept { return hasShadowBuffer; }
	/**
	 * @brief Use ambient occlusion buffer for PBR lighting rendering.
	 */
	bool useAoBuffer() const noexcept { return hasAoBuffer; }
	/**
	 * @brief Enables or disables use of the shadow and ambient occlusion buffers.
	 * @details It destroys existing buffers on use set to false.
	 * 
	 * @param useShadowBuffer use shadow buffer for rendering
	 * @param useAoBuffer use ambient occlusion buffer for rendering
	 */
	void setConsts(bool useShadowBuffer, bool useAoBuffer);

	/**
	 * @brief Returns PBR lighting graphics pipeline.
	 */
	ID<GraphicsPipeline> getLightingPipeline();
	/**
	 * @brief Returns PBR lighting IBL specular graphics pipeline. (Image Based Lighting)
	 */
	ID<ComputePipeline> getIblSpecularPipeline();
	/**
	 * @brief Returns PBR lighting AO denoise graphics pipeline. (Ambient Occlusion)
	 */
	ID<GraphicsPipeline> getAoDenoisePipeline();

	/**
	 * @brief Returns PBR lighting shadow framebuffer array.
	 */
	const ID<Framebuffer>* getShadowFramebuffers();
	/**
	 * @brief Returns PBR lighting AO framebuffer array. (Ambient Occlusion)
	 */
	const ID<Framebuffer>* getAoFramebuffers();

	/**
	 * @brief Returns PBR lighting DFG LUT image. (DFG Look Up Table)
	 */
	ID<Image> getDfgLUT();
	/**
	 * @brief Returns PBR lighting shadow buffer.
	 */
	ID<Image> getShadowBuffer();
	/**
	 * @brief Returns PBR lighting AO buffer. (Ambient Occlusion)
	 */
	ID<Image> getAoBuffer();
	/**
	 * @brief Returns PBR lighting shadow image view array.
	 */
	const ID<ImageView>* getShadowImageViews();
	/**
	 * @brief Returns PBR lighting AO image view array. (Ambient Occlusion)
	 */
	const ID<ImageView>* getAoImageViews();

	/*******************************************************************************************************************
	 * @brief Loads cubemap rendering data from the resource pack.
	 * @note Loads from the scenes directory in debug build.
	 * 
	 * @param[in] path target cubemap resource path
	 * @param[out] cubemap cubemap image instance
	 * @param[out] sh spherical harmonics buffer instance
	 * @param[out] specular specular cubemap instance
	 * @param strategy graphics memory allocation strategy
	 */
	void loadCubemap(const fs::path& path, Ref<Image>& cubemap, Ref<Buffer>& sh,
		Ref<Image>& specular, Memory::Strategy strategy = Memory::Strategy::Size);

	/**
	 * @brief Creates PBR lighting descriptor set.
	 * 
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular);
};

} // namespace garden