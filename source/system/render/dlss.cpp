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

#include "garden/system/render/dlss.hpp"

#if GARDEN_NVIDIA_DLSS
#include "garden/graphics/vulkan/command-buffer.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/log.hpp"
#include "mpio/directory.hpp"

#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_vk.h"
#include <iostream>

using namespace garden;

DlssRenderSystem::DlssRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", DlssRenderSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", DlssRenderSystem::postDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("PreLdrRender", DlssRenderSystem::preLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", DlssRenderSystem::swapchainRecreate);
}
DlssRenderSystem::~DlssRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", DlssRenderSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostDeinit", DlssRenderSystem::postDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreLdrRender", DlssRenderSystem::preLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", DlssRenderSystem::swapchainRecreate);
	}

	unsetSingleton();
}

static void onDlssLog(const char* message, NVSDK_NGX_Logging_Level loggingLevel, NVSDK_NGX_Feature sourceComponent)
{
	std::cout << message;
}

//**********************************************************************************************************************
static NVSDK_NGX_Resource_VK imageToNgxResource(VulkanAPI* vulkanAPI, 
	VulkanCommandBuffer* vkCommandBuffer, ID<ImageView> imageView)
{
	Image::BarrierState newImageState;
	newImageState.access = (uint32)vk::AccessFlagBits::eShaderWrite;
	newImageState.layout = (uint32)vk::ImageLayout::eGeneral;
	newImageState.stage = (uint32)vk::PipelineStageFlagBits::eComputeShader;
	vkCommandBuffer->addImageBarrier(vulkanAPI, newImageState, imageView);

	auto imageViewView = vulkanAPI->imageViewPool.get(imageView);
	auto baseImageView = vulkanAPI->imagePool.get(imageViewView->getImage());
	auto isStorage = hasAnyFlag(baseImageView->getUsage(), Image::Usage::Storage);
	auto imageSize = imageViewView->calcSize();

    VkImageSubresourceRange subresourceRange;
	subresourceRange.aspectMask = isFormatDepthOrStencil(imageViewView->getFormat()) ?
		VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = imageViewView->getBaseMip();
	subresourceRange.levelCount = imageViewView->getMipCount();
	subresourceRange.baseArrayLayer = imageViewView->getBaseLayer();
	subresourceRange.layerCount = imageViewView->getLayerCount();

    return NVSDK_NGX_Create_ImageView_Resource_VK(
		(VkImageView)ResourceExt::getInstance(**imageViewView), (VkImage)ResourceExt::getInstance(**baseImageView), 
		subresourceRange, (VkFormat)toVkFormat(imageViewView->getFormat()), imageSize.x, imageSize.y, isStorage);
}

//**********************************************************************************************************************
static void terminateDlss(NVSDK_NGX_Parameter* ngxParameters)
{
	if (!ngxParameters)
		return;

	NVSDK_NGX_VULKAN_DestroyParameters(ngxParameters);
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		NVSDK_NGX_VULKAN_Shutdown1(VulkanAPI::get()->device);
	else abort();
}
static NVSDK_NGX_Parameter* initializeDlss(const wstring& nvidiaDlssPath)
{
	NVSDK_NGX_FeatureCommonInfo* featureCommonInfoPtr = nullptr;
	#if 0 // Note: for DLSS debugging.
	NVSDK_NGX_FeatureCommonInfo featureCommonInfo;
	memset(&featureCommonInfo, 0, sizeof(NVSDK_NGX_FeatureCommonInfo));
	featureCommonInfo.LoggingInfo.LoggingCallback = onDlssLog;
	featureCommonInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
	featureCommonInfoPtr = &featureCommonInfo;
	#endif

	NVSDK_NGX_Result ngxResult; NVSDK_NGX_Parameter* ngxParameters = nullptr;
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		#if defined(GARDEN_NVIDIA_DLSS_PROJECT_ID)
		ngxResult = NVSDK_NGX_VULKAN_Init_with_ProjectID(GARDEN_NVIDIA_DLSS_PROJECT_ID,
			NVSDK_NGX_ENGINE_TYPE_CUSTOM,  GARDEN_VERSION_STRING, nvidiaDlssPath.c_str(), 
			vulkanAPI->instance, vulkanAPI->physicalDevice, vulkanAPI->device,
			nullptr, nullptr, featureCommonInfoPtr);
		#elif defined(GARDEN_NVIDIA_DLSS_APPLICATION_ID)
		ngxResult = NVSDK_NGX_VULKAN_Init(GARDEN_NVIDIA_DLSS_APPLICATION_ID, 
			nvidiaDlssPath.c_str(), vulkanAPI->instance, vulkanAPI->physicalDevice, 
			vulkanAPI->device, nullptr, nullptr, &featureCommonInfoPtr);
		#endif

		if (ngxResult != NVSDK_NGX_Result_Success)
		{
			auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
			GARDEN_LOG_WARN("Failed to initialize Nvidia DLSS. ("
				"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
			return nullptr;
		}

		ngxResult = NVSDK_NGX_VULKAN_GetCapabilityParameters(&ngxParameters);
	}
	else abort();

	if (ngxResult != NVSDK_NGX_Result_Success)
	{
		auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
		GARDEN_LOG_WARN("Failed to get Nvidia DLSS parameters. ("
			"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
			NVSDK_NGX_VULKAN_Shutdown1(VulkanAPI::get()->device);
		return nullptr;
	}

	int dlssAvailable = 0;
	ngxResult = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
	if (ngxResult != NVSDK_NGX_Result_Success || !dlssAvailable)
	{
		NVSDK_NGX_Parameter_GetI(ngxParameters, 
			NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, (int*)&ngxResult);
		auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
		GARDEN_LOG_WARN("Nvidia DLSS is not available on this device. ("
			"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
		terminateDlss(ngxParameters);
		return nullptr;
	}

	return ngxParameters;
}

//**********************************************************************************************************************
static NVSDK_NGX_Handle* createDlssFeature(CommandBuffer* commandBuffer, NVSDK_NGX_Parameter* ngxParameters, 
	NVSDK_NGX_PerfQuality_Value perfQuality, NVSDK_NGX_DLSS_Hint_Render_Preset renderPreset, 
	uint2& optimalSize, uint2& minSize, uint2& maxSize, float& sharpness)
{
	auto framebufferSize = GraphicsSystem::Instance::get()->getFramebufferSize();
	unsigned int optimalWidth = 0, optimalHeight = 0, maxWidth = 0, 
		maxHeight = 0, minWidth = 0, minHeight = 0;
	auto ngxResult = NGX_DLSS_GET_OPTIMAL_SETTINGS(ngxParameters, framebufferSize.x, framebufferSize.y,
		perfQuality, &optimalWidth, &optimalHeight, &maxWidth, &maxHeight, &minWidth, &minHeight, &sharpness);
	if (ngxResult != NVSDK_NGX_Result_Success)
	{
		optimalWidth = maxWidth = minWidth = framebufferSize.x;
		optimalHeight = maxHeight = minHeight = framebufferSize.y; sharpness = 0.0f;
		GARDEN_LOG_ERROR("Failed to get Nvidia DLSS optimal settings.");
	}

	NVSDK_NGX_DLSS_Create_Params createParams;
	memset(&createParams, 0, sizeof(NVSDK_NGX_DLSS_Create_Params));
	createParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | 
		NVSDK_NGX_DLSS_Feature_Flags_AutoExposure | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
	// TODO: try to pass exposure manually. ExposureValue = MidGray / (AverageLuma * (1.0 - MidGray))

	if (DeferredRenderSystem::Instance::tryGet())
		createParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

	createParams.Feature.InTargetWidth = framebufferSize.x;
	createParams.Feature.InTargetHeight = framebufferSize.y;
	createParams.Feature.InWidth = optimalWidth;
	createParams.Feature.InHeight = optimalHeight;
	createParams.Feature.InPerfQualityValue = perfQuality;

	// TODO: support variable ratio

	// TODO:
	// NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, renderPreset);
    // NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, renderPreset);
    // NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, renderPreset);
    // NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, renderPreset);
    // NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, renderPreset);

	NVSDK_NGX_Handle* ngxFeature = nullptr;
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vkCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
		ngxResult = NGX_VULKAN_CREATE_DLSS_EXT(vkCommandBuffer->instance, 
			1, 1, &ngxFeature, ngxParameters, &createParams);
	}
	else abort();

	if (ngxResult != NVSDK_NGX_Result_Success)
	{
		auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
		GARDEN_LOG_ERROR("Failed to create Nvidia DLSS instance. ("
			"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
		return nullptr;
	}

	optimalSize = uint2(optimalWidth, optimalHeight);
	minSize = uint2(minWidth, minHeight);
	maxSize = uint2(maxWidth, maxHeight);
	return ngxFeature;
}
static void destroyDlssFeature(NVSDK_NGX_Handle* ngxFeature)
{
	if (!ngxFeature)
		return;
	
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		NVSDK_NGX_VULKAN_ReleaseFeature(ngxFeature);
	else abort();
}

//**********************************************************************************************************************
void DlssRenderSystem::preInit()
{
	auto graphicsAPI = GraphicsAPI::get();
	auto appInfoSystem = AppInfoSystem::Instance::get();
	auto appDataPath = mpio::Directory::getAppDataPath(appInfoSystem->getAppDataName());
	auto nvidiaDlssPath = (appDataPath / "nvidia").generic_wstring();

	if (!fs::exists(nvidiaDlssPath))
		fs::create_directory(nvidiaDlssPath);

	NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo;
	memset(&discoveryInfo, 0, sizeof(NVSDK_NGX_FeatureDiscoveryInfo));
	discoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
	discoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
	discoveryInfo.ApplicationDataPath = nvidiaDlssPath.c_str();

	#if defined(GARDEN_NVIDIA_DLSS_PROJECT_ID)
	discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
	discoveryInfo.Identifier.v.ProjectDesc.ProjectId = GARDEN_NVIDIA_DLSS_PROJECT_ID;
	discoveryInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
	discoveryInfo.Identifier.v.ProjectDesc.EngineVersion = GARDEN_VERSION_STRING;
	#elif defined(GARDEN_NVIDIA_DLSS_APPLICATION_ID)
	discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
	discoveryInfo.Identifier.v.ApplicationId = GARDEN_NVIDIA_DLSS_APPLICATION_ID;
	#endif

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		threadSystem->getBackgroundPool().addTask([discoveryInfo](const ThreadPool::Task& task)
		{
			auto ngxResult = NVSDK_NGX_UpdateFeature(&discoveryInfo.Identifier, discoveryInfo.FeatureID);
			auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
			auto resultString = "NVSDK_NGX_UpdateFeature: " + string(ngxResStr.begin(), ngxResStr.end());
			if (ngxResult == NVSDK_NGX_Result_Success) GARDEN_LOG_INFO(resultString);
			else GARDEN_LOG_WARN(resultString);
		});
	}

	NVSDK_NGX_FeatureRequirement featureRequirement;
	memset(&featureRequirement, 0, sizeof(NVSDK_NGX_FeatureRequirement));

	NVSDK_NGX_Result ngxResult;
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		if (!vulkanAPI->features.hasNvidiaDlss)
			return;
		
		ngxResult = NVSDK_NGX_VULKAN_GetFeatureRequirements(vulkanAPI->instance, 
			vulkanAPI->physicalDevice, &discoveryInfo, &featureRequirement);
	}
	else abort();

	if (ngxResult != NVSDK_NGX_Result_Success)
	{
		auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
		GARDEN_LOG_WARN("Failed to get Nvidia DLSS requirements. ("
			"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
		return;
	}
	if (featureRequirement.FeatureSupported != 0)
	{
		GARDEN_LOG_WARN("Nvidia DLSS is not supported. ("
			"reason: " + to_string(featureRequirement.FeatureSupported) + ")");
		return;
	}

	parameters = initializeDlss(nvidiaDlssPath);
	if (!parameters)
		return;
	GARDEN_LOG_INFO("Initialized Nvidia DLSS.");
}
void DlssRenderSystem::postDeinit()
{
	destroyDlssFeature((NVSDK_NGX_Handle*)feature);
	terminateDlss((NVSDK_NGX_Parameter*)parameters);
}

//**********************************************************************************************************************
void DlssRenderSystem::createDlssFeatureCommand(void* commandBuffer, void* argument)
{
	auto dlssSystem = (DlssRenderSystem*)argument;
	GARDEN_ASSERT(!dlssSystem->feature);

	dlssSystem->feature = createDlssFeature((CommandBuffer*)commandBuffer, 
		(NVSDK_NGX_Parameter*)dlssSystem->parameters, (NVSDK_NGX_PerfQuality_Value)dlssSystem->quality, {},
		dlssSystem->optimalSize, dlssSystem->minSize, dlssSystem->maxSize, dlssSystem->sharpness);
	GARDEN_LOG_INFO("Recreated Nvidia DLSS feature. (optimalSize: " + toString(dlssSystem->optimalSize) + ")");
}
void DlssRenderSystem::evaluateDlssCommand(void* commandBuffer, void* argument)
{
	auto dlssSystem = (DlssRenderSystem*)argument;
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (!dlssSystem->feature || deferredSystem->getHdrFramebuffer() == deferredSystem->getUpscaleHdrFramebuffer())
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	if (gFramebufferView->getSize() != dlssSystem->optimalSize)
		return;

	auto& jitterOffsets = graphicsSystem->getJitterOffsets();
	auto jitterOffset = jitterOffsets[graphicsSystem->getCurrentFrameIndex() % jitterOffsets.size()];
	auto hdrFramebufferView = graphicsSystem->get(deferredSystem->getHdrFramebuffer());
	auto upscaleHdrFramebufferView = graphicsSystem->get(deferredSystem->getUpscaleHdrFramebuffer());
	auto inputSize = hdrFramebufferView->getSize();

	NVSDK_NGX_Result ngxResult;
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		NVSDK_NGX_VK_DLSS_Eval_Params evalParams;
		memset(&evalParams, 0, sizeof(NVSDK_NGX_VK_DLSS_Eval_Params));

		auto vulkanAPI = VulkanAPI::get();
		auto vkCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
		auto inputResource = imageToNgxResource(vulkanAPI, vkCommandBuffer,	
			hdrFramebufferView->getColorAttachments()[0].imageView);
		auto outputResource = imageToNgxResource(vulkanAPI, vkCommandBuffer, 
			upscaleHdrFramebufferView->getColorAttachments()[0].imageView);
		auto depthResource = imageToNgxResource(vulkanAPI, vkCommandBuffer, 
			gFramebufferView->getDepthStencilAttachment().imageView);
		auto velocityResource = imageToNgxResource(vulkanAPI, vkCommandBuffer, 
			gFramebufferView->getColorAttachments()[DeferredRenderSystem::gBufferVelocity].imageView);
		vkCommandBuffer->processPipelineBarriers(vulkanAPI);

		evalParams.Feature.pInColor = &inputResource;
		evalParams.Feature.pInOutput = &outputResource;
		evalParams.pInDepth = &depthResource;
		evalParams.pInMotionVectors = &velocityResource;
		// TODO: evalParams.pInExposureTexture = &exposureResource;
		evalParams.InJitterOffsetX = jitterOffset.x;
		evalParams.InJitterOffsetY = jitterOffset.y;
		evalParams.InReset = graphicsSystem->isTeleported() ? 1 : 0;
		evalParams.InRenderSubrectDimensions.Width = inputSize.x;
		evalParams.InRenderSubrectDimensions.Height = inputSize.y;
		evalParams.InMVScaleX = inputSize.x * -0.5f;
		evalParams.InMVScaleY = inputSize.y * -0.5f;

		ngxResult = NGX_VULKAN_EVALUATE_DLSS_EXT(
			vkCommandBuffer->instance, (NVSDK_NGX_Handle*)dlssSystem->feature, 
			(NVSDK_NGX_Parameter*)dlssSystem->parameters, &evalParams);
	}
	else abort();

	if (ngxResult != NVSDK_NGX_Result_Success)
	{
		auto ngxResStr = wstring(GetNGXResultAsString(ngxResult));
		GARDEN_LOG_ERROR("Failed to evaluate Nvidia DLSS. ("
			"result: " + string(ngxResStr.begin(), ngxResStr.end()) + ")");
	}
}

//**********************************************************************************************************************
void DlssRenderSystem::preLdrRender()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!parameters || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		BEGIN_GPU_DEBUG_LABEL("DLSS Evaluate", Color::transparent);
		if (!feature)
			graphicsSystem->customCommand(createDlssFeatureCommand, this);
		graphicsSystem->customCommand(evaluateDlssCommand, this);
	}
	graphicsSystem->stopRecording();

	if (feature)
	{
		graphicsSystem->setScaledFrameSize(optimalSize);
		graphicsSystem->setMipLodBias(calcMipLodBias());
		graphicsSystem->useUpscaling = graphicsSystem->useJittering = true;
	}
}

void DlssRenderSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize && maxSize != graphicsSystem->getFramebufferSize())
	{
		destroyDlssFeature((NVSDK_NGX_Handle*)feature);
		feature = {};
	}
}

void DlssRenderSystem::setQuality(DlssQuality quality)
{
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		VulkanAPI::get()->device.waitIdle();
	else abort();

	destroyDlssFeature((NVSDK_NGX_Handle*)feature);
	feature = {};
	this->quality = quality;
}

float DlssRenderSystem::calcMipLodBias(float nativeBias) noexcept
{
	return nativeBias + std::log2((float)optimalSize.x / maxSize.x) - 1.0f + FLT_EPSILON;
}

#endif // GARDEN_NVIDIA_DLSS