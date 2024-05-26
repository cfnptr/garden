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

#pragma once
#include "garden/system/render/deferred.hpp"

namespace garden
{

// TODO: I have failed to find good denoiser for shadows. Research this field.

const uint8 shadowBufferCount = 1;
const uint8 aoBufferCount = 2;

using namespace garden::graphics;
class LightingRenderSystem;

// TODO: TODO: check if correct.
//float iorToReflectance(float transmittedIor, float incidentIor = 1.0)
//{
// float f0 = sqrt((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
// return sqrt(f0 / 0.16);
//}

/***********************************************************************************************************************
 * @brief PBR lighting rendering data container.
 */
struct LightingRenderComponent final : public Component
{
	Ref<Image> cubemap = {};
	Ref<Buffer> sh = {};
	Ref<Image> specular = {};
	Ref<DescriptorSet> descriptorSet = {};
};

/**
 * @brief Shadow rendering system interface.
 */
class IShadowRenderSystem
{
protected:
	virtual void preShadowRender() { }
	virtual bool shadowRender() = 0;
	friend class LightingRenderSystem;
};
/**
 * @brief Ambient occlusion rendering system interface.
 */
class IAoRenderSystem
{
protected:
	virtual void preAoRender() { }
	virtual bool aoRender() = 0;
	friend class LightingRenderSystem;
};

// TODO: allow to disable shadow or ao buffers.

/***********************************************************************************************************************
 * @brief PBR lighting rendering system.
 */
class LightingRenderSystem final : public System
{
	DeferredRenderSystem* deferredSystem = nullptr;
	LinearPool<LightingRenderComponent, false> components;
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
	 * @param useShadowBuffer use shadow buffer for rendering
	 * @param useAoBuffer use ambient occlusion buffer for rendering
	 */
	LightingRenderSystem(bool useShadowBuffer = false, bool useAoBuffer = false);
	/**
	 * @brief Destroys lighting rendering system instance.
	 */
	~LightingRenderSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	friend class ecsm::Manager;
public:
	float4 shadowColor = float4(1.0f);

	bool useShadowBuffer() const noexcept { return hasShadowBuffer; }
	bool useAoBuffer() const noexcept { return hasAoBuffer; }
	void setConsts(bool useShadowBuffer, bool useAoBuffer);

	ID<GraphicsPipeline> getLightingPipeline();
	ID<ComputePipeline> getIblSpecularPipeline();
	ID<GraphicsPipeline> getAoDenoisePipeline();

	const ID<Framebuffer>* getShadowFramebuffers();
	const ID<Framebuffer>* getAoFramebuffers();

	ID<Image> getDfgLUT();
	ID<Image> getShadowBuffer();
	ID<Image> getAoBuffer();
	const ID<ImageView>* getShadowImageViews();
	const ID<ImageView>* getAoImageViews();

	void loadCubemap(const fs::path& path, Ref<Image>& cubemap, Ref<Buffer>& sh,
		Ref<Image>& specular, Memory::Strategy strategy = Memory::Strategy::Size);
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular);
};

} // namespace garden