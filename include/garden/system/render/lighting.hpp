//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/system/render/deferred.hpp"

/*
namespace garden
{

// TODO: I have failed to find good denoiser for shadows. Research this field.
#define SHADOW_BUFFER_COUNT 1
#define AO_BUFFER_COUNT 2

using namespace garden;
using namespace garden::graphics;
class LightingRenderSystem;

// TODO: TODO: check if correct.
//float iorToReflectance(float transmittedIor, float incidentIor = 1.0)
//{
// float f0 = sqrt((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
// return sqrt(f0 / 0.16);
//}

//--------------------------------------------------------------------------------------------------
struct LightingRenderComponent final : public Component
{
	Ref<Image> cubemap = {};
	Ref<Buffer> sh = {};
	Ref<Image> specular = {};
	Ref<DescriptorSet> descriptorSet = {};
};

//--------------------------------------------------------------------------------------------------
class IShadowRenderSystem
{
protected:
	virtual void preShadowRender() { }
	virtual bool shadowRender() = 0;

	friend class LightingRenderSystem;
public:
	LightingRenderSystem* getLightingSystem() noexcept
	{
		GARDEN_ASSERT(lightingSystem);
		return lightingSystem;
	}
	const LightingRenderSystem* getLightingSystem() const noexcept
	{
		GARDEN_ASSERT(lightingSystem);
		return lightingSystem;
	}
};
class IAoRenderSystem
{
protected:
	virtual void preAoRender() { }
	virtual bool aoRender() = 0;

	friend class LightingRenderSystem;
public:
	LightingRenderSystem* getLightingSystem() noexcept
	{
		GARDEN_ASSERT(lightingSystem);
		return lightingSystem;
	}
	const LightingRenderSystem* getLightingSystem() const noexcept
	{
		GARDEN_ASSERT(lightingSystem);
		return lightingSystem;
	}
};

// TODO: allow to disable shadow or ao buffers.

//--------------------------------------------------------------------------------------------------
class LightingRenderSystem final : public System,
	public IRenderSystem, public IDeferredRenderSystem
{
	LinearPool<LightingRenderComponent, false> components;
	ID<Image> dfgLUT = {};
	ID<Image> shadowBuffer = {};
	ID<Image> aoBuffer = {};
	ID<ImageView> shadowImageViews[SHADOW_BUFFER_COUNT] = {};
	ID<ImageView> aoImageViews[AO_BUFFER_COUNT] = {};
	ID<Framebuffer> shadowFramebuffers[SHADOW_BUFFER_COUNT] = {};
	ID<Framebuffer> aoFramebuffers[AO_BUFFER_COUNT] = {};
	ID<GraphicsPipeline> lightingPipeline = {};
	ID<ComputePipeline> iblSpecularPipeline = {};
	ID<GraphicsPipeline> aoDenoisePipeline = {};
	ID<DescriptorSet> lightingDescriptorSet = {};
	ID<DescriptorSet> aoDenoiseDescriptorSet = {};
	bool useShadowBuffer = false;
	bool useAoBuffer = false;
	uint16 _alignment = 0;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;

	void preHdrRender() final;
	void hdrRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	friend class ecsm::Manager;
	friend class LightingEditorSystem;
public:
	float4 shadowColor = float4(1.0f);

	bool getUseShadowBuffer() const noexcept { return useShadowBuffer; }
	bool getUseAoBuffer() const noexcept { return useAoBuffer; }
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

	void loadCubemap(const fs::path& path,
		Ref<Image>& cubemap, Ref<Buffer>& sh, Ref<Image>& specular,
		Memory::Strategy strategy = Memory::Strategy::Size);
	Ref<DescriptorSet> createDescriptorSet(ID<Buffer> sh, ID<Image> specular);
};

} // namespace garden
*/