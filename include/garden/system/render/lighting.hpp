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
 * @brief Lighting rendering functions.
 */

// TODO: I have failed to find good denoiser for shadows. Research this field.

#pragma once
#include "garden/system/render/deferred.hpp"

namespace garden
{

using namespace garden::graphics;

// TODO: TODO: check if correct.
//float iorToReflectance(float transmittedIor, float incidentIor = 1.0)
//{
// float f0 = sqrt((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
// return sqrt(f0 / 0.16);
//}

/**
 * @brief PBR lighting rendering data container.
 */
struct LightingRenderComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< Lighting cubemap image. */
	Ref<Buffer> sh = {};                   /**< Lighting spherical harmonics buffer. */
	Ref<Image> specular = {};              /**< Lighting specular cubemap. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Lighting descriptor set. */

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

	friend class LightingRenderSystem;
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

	friend class LightingRenderSystem;
};

/***********************************************************************************************************************
 * @brief PBR lighting rendering system.
 */
class LightingRenderSystem final : public ComponentSystem<LightingRenderComponent>, 
	public Singleton<LightingRenderSystem>
{
public:
	/**
	 * @brief Lighting rendering shadow buffer count.
	 */
	static constexpr uint8 shadowBufferCount = 1;
	/**
	 * @brief Lighting rendering AO buffer count. (Ambient Occlusion)
	 */
	static constexpr uint8 aoBufferCount = 2;

	struct LightingPC final
	{
		float4x4 uvToWorld;
		float4 shadowColor;
	};
	struct SpecularPC final
	{
		uint32 count;
	};
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

	/**
	 * @brief Creates a new lighting rendering system instance.
	 * 
	 * @param useShadowBuffer create and use shadow buffer for rendering
	 * @param useAoBuffer create use ambient occlusion buffer for rendering
	 * @param setSingleton set system singleton instance
	 */
	LightingRenderSystem(bool useShadowBuffer = false, bool useAoBuffer = false, bool setSingleton = true);
	/**
	 * @brief Destroys lighting rendering system instance.
	 */
	~LightingRenderSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();

	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;

	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Shadow color factor. (RGBA)
	 */
	float4 shadowColor = float4(1.0f);

	/**
	 * @brief Use shadow buffer for lighting rendering.
	 */
	bool useShadowBuffer() const noexcept { return hasShadowBuffer; }
	/**
	 * @brief Use ambient occlusion buffer for lighting rendering.
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
	 * @brief Returns lighting graphics pipeline instance.
	 */
	ID<GraphicsPipeline> getLightingPipeline();
	/**
	 * @brief Returns IBL specular graphics pipeline instance. (Image Based Lighting)
	 */
	ID<ComputePipeline> getIblSpecularPipeline();
	/**
	 * @brief Returns AO denoise graphics pipeline instance.
	 */
	ID<GraphicsPipeline> getAoDenoisePipeline();

	/**
	 * @brief Returns shadow framebuffer array.
	 */
	const ID<Framebuffer>* getShadowFramebuffers();
	/**
	 * @brief Returns ambient occlusion framebuffer array.
	 */
	const ID<Framebuffer>* getAoFramebuffers();

	/**
	 * @brief Returns DFG LUT image instance. (DFG Look Up Table)
	 */
	ID<Image> getDfgLUT();
	/**
	 * @brief Returns shadow buffer instance.
	 */
	ID<Image> getShadowBuffer();
	/**
	 * @brief Returns ambient occlusion buffer instance.
	 */
	ID<Image> getAoBuffer();
	/**
	 * @brief Returns shadow image view array.
	 */
	const ID<ImageView>* getShadowImageViews();
	/**
	 * @brief Returns ambient occlusion image view array.
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
	 * @brief Creates lighting descriptor set.
	 * 
	 * @param sh spherical harmonics buffer instance
	 * @param specular specular cubemap instance
	 */
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular);
};

} // namespace garden