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

#define VMA_IMPLEMENTATION
#define VOLK_IMPLEMENTATION

#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/vulkan/command-buffer.hpp"
#include "garden/graphics/glfw.hpp" // Note: Do not move it.
#include "garden/hash.hpp"
#include "mpio/directory.hpp"

#if GARDEN_NVIDIA_DLSS
#include "nvsdk_ngx_vk.h"
#endif

#include <vector>
#include <fstream>
#include <iostream>

using namespace garden;

#if GARDEN_DEBUG
constexpr vk::DebugUtilsMessageSeverityFlagsEXT debugMessageSeverity =
	//vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
constexpr vk::DebugUtilsMessageTypeFlagsEXT debugMessageType =
	vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
	vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
#endif

#if GARDEN_DEBUG
//**********************************************************************************************************************
static vk::Bool32 VKAPI_PTR vkDebugMessengerCallback(
	vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
	const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData)
{
	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning &&
		string_view(callbackData->pMessage).find("VK_LAYER_VALVE_steam_") != string::npos)
	{
		return VK_FALSE; // Skipping Steam layers API version 1.3 warnings.
	}

	const char* severity;
	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
		severity = "VERBOSE";
	else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
		severity = "INFO";
	else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		severity = "WARNING";
	else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		severity = "ERROR";
	else
		severity = "UNKNOWN";
	cout << "VULKAN::" + string(severity) + ": ";

	for (int32 i = callbackData->cmdBufLabelCount - 1; i >= 0; i--)
		cout << callbackData->pCmdBufLabels[i].pLabelName + string(" -> ");
	cout << callbackData->pMessage + string("\n") << endl;

	// Debuging break points \/
	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		return VK_FALSE; // warning
	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		return VK_FALSE; // ERROR
	return VK_FALSE;
}
#endif

static bool hasExtension(const vector<const char*>& extensions, const char* extension) noexcept
{
	for (auto check : extensions)
	{
		if (strcmp(check, extension) == 0)
			return true;
	}

	return false;
}

#if GARDEN_NVIDIA_DLSS
static NVSDK_NGX_FeatureDiscoveryInfo getDlssDiscoveryInfo()
{
	NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo;
	memset(&discoveryInfo, 0, sizeof(NVSDK_NGX_FeatureDiscoveryInfo));
	discoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
	discoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
	discoveryInfo.ApplicationDataPath = L".";

	#if defined(GARDEN_NVIDIA_DLSS_PROJECT_ID)
	discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
	discoveryInfo.Identifier.v.ProjectDesc.ProjectId = GARDEN_NVIDIA_DLSS_PROJECT_ID;
	discoveryInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
	discoveryInfo.Identifier.v.ProjectDesc.EngineVersion = GARDEN_VERSION_STRING;
	#elif defined(GARDEN_NVIDIA_DLSS_APP_ID)
	discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
	discoveryInfo.Identifier.v.ApplicationId = GARDEN_NVIDIA_DLSS_APP_ID;
	#endif

	return discoveryInfo;
}
#endif

//**********************************************************************************************************************
static vk::Instance createVkInstance(const string& appName, Version appVersion,
	uint32& instanceVersionMajor, uint32& instanceVersionMinor, VulkanAPI::Features& features)
{
	auto getInstanceVersion = (PFN_vkEnumerateInstanceVersion)
		vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
	if (!getInstanceVersion)
		throw GardenError("Vulkan API 1.0 version is not supported.");

	uint32 installedVersion = 0;
	auto vkResult = (vk::Result)getInstanceVersion(&installedVersion);
	if (vkResult != vk::Result::eSuccess)
		throw GardenError("Failed to get Vulkan version.");

	instanceVersionMajor = VK_API_VERSION_MAJOR(installedVersion);
	instanceVersionMinor = VK_API_VERSION_MINOR(installedVersion);

	if (instanceVersionMajor <= 1 && instanceVersionMinor <= 1)
		throw GardenError("Vulkan API 1.1 version is not supported.");

	auto vkEngineVersion = VK_MAKE_API_VERSION(0,
		GARDEN_VERSION_MAJOR, GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
	auto vkAppVersion = VK_MAKE_API_VERSION(0,
		appVersion.major, appVersion.minor, appVersion.patch);
	auto instanceVersion = VK_MAKE_API_VERSION(0,
		instanceVersionMajor, instanceVersionMinor, 0);
	vk::ApplicationInfo appInfo(appName.c_str(), vkAppVersion,
		GARDEN_NAME_STRING, vkEngineVersion, instanceVersion);
	
	vector<const char*> extensions;
	vector<const char*> layers;

	uint32 glfwExtensionCount = 0;
	auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	extensions.reserve(glfwExtensionCount);

	for (uint32 i = 0; i < glfwExtensionCount; i++)
		extensions.push_back(glfwExtensions[i]);

	#if GARDEN_OS_MACOS
	if (!hasExtension(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	#endif

	auto extensionProperties = vk::enumerateInstanceExtensionProperties();
	auto layerProperties = vk::enumerateInstanceLayerProperties();
	auto isNvidiaGPU = false; // Skipping Nvidia related calls for other vendors.
	
	for	(const auto& properties : layerProperties)
	{
		auto layerName = string_view(properties.layerName);
		if (layerName == "VK_LAYER_MESA_anti_lag")
			layers.push_back("VK_LAYER_MESA_anti_lag");
		#if GARDEN_GAPI_VALIDATIONS
		else if (layerName == "VK_LAYER_KHRONOS_validation")
			layers.push_back("VK_LAYER_KHRONOS_validation");
		#endif

		#if GARDEN_NVIDIA_DLSS
		if (layerName.find("VK_LAYER_NV_") != string::npos)
			isNvidiaGPU = true;
		#endif
	}

	const void* instanceInfoNext = nullptr;
	#if GARDEN_DEBUG
	for	(const auto& properties : extensionProperties)
	{
		auto extensionName = string_view(properties.extensionName);
		if (extensionName == VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
			features.debugUtils = true;

		#if GARDEN_NVIDIA_DLSS
		if (extensionName.find("VK_NV_") != string::npos)
			isNvidiaGPU = true;
		#endif
	}

	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo;
	if (features.debugUtils)
	{
		if (!hasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		debugUtilsInfo.messageSeverity = debugMessageSeverity;
		debugUtilsInfo.messageType = debugMessageType;
		debugUtilsInfo.pfnUserCallback = vkDebugMessengerCallback;
		instanceInfoNext = &debugUtilsInfo;
	}
	#endif

	#if GARDEN_NVIDIA_DLSS
	if (isNvidiaGPU)
	{
		auto dlssDiscoveryInfo = getDlssDiscoveryInfo();
		uint32_t dlssExtensionCount; VkExtensionProperties* dlssExtensions;
		auto ngxResult = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
			&dlssDiscoveryInfo, &dlssExtensionCount, &dlssExtensions);
		features.nvidiaDlss = ngxResult == NVSDK_NGX_Result_Success;

		if (features.nvidiaDlss)
		{
			for (uint32 i = 0; i < dlssExtensionCount; i++)
			{
				auto hasDlssExtension = false;
				for	(const auto& properties : extensionProperties)
				{
					if (string_view(properties.extensionName) != dlssExtensions[i].extensionName)
						continue;
					hasDlssExtension = true;
					break;
				}

				if (!hasDlssExtension)
				{
					features.nvidiaDlss = false;
					break;
				}
			}

			if (features.nvidiaDlss)
			{
				for (uint32 i = 0; i < dlssExtensionCount; i++)
				{
					if (!hasExtension(extensions, dlssExtensions[i].extensionName))
						extensions.push_back(dlssExtensions[i].extensionName);
				}
			}
		}
	}
	#endif

	#if GARDEN_OS_MACOS
	auto flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
	#else
	auto flags = vk::InstanceCreateFlags();
	#endif

	vk::InstanceCreateInfo instanceInfo(flags, &appInfo, layers, extensions, instanceInfoNext);
	auto instance = vk::createInstance(instanceInfo);
	volkLoadInstance(instance);
	return instance;
}

#if GARDEN_DEBUG
static vk::DebugUtilsMessengerEXT createVkDebugMessenger(vk::Instance instance)
{
	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo;
	debugUtilsInfo.messageSeverity = debugMessageSeverity;
	debugUtilsInfo.messageType = debugMessageType;
	debugUtilsInfo.pfnUserCallback = vkDebugMessengerCallback;
	return instance.createDebugUtilsMessengerEXT(debugUtilsInfo, nullptr);
}
#endif

//**********************************************************************************************************************
static vk::PhysicalDevice getBestPhysicalDevice(vk::Instance instance)
{
	auto devices = instance.enumeratePhysicalDevices();

	if (devices.empty())
		throw GardenError("No suitable physical device.");

	uint32 targetIndex = 0, targetScore = 0;

	if (devices.size() > 1)
	{
		for (uint32 i = 0; i < (uint32)devices.size(); i++)
		{
			auto properties = devices[i].getProperties();

			uint32 score;
			switch (properties.deviceType)
			{
				case vk::PhysicalDeviceType::eDiscreteGpu: score = 100000; break;
				case vk::PhysicalDeviceType::eVirtualGpu: score = 90000; break;
				case vk::PhysicalDeviceType::eIntegratedGpu: score = 80000; break;
				case vk::PhysicalDeviceType::eCpu: score = 70000; break;
				default: score = 0;
			}

			score += properties.limits.maxImageDimension2D;
			// TODO: add other tests

			if (score > targetScore)
			{
				targetIndex = i;
				targetScore = score;
			}
		}
	}

	return devices[targetIndex];
}

static vk::SurfaceKHR createVkSurface(vk::Instance instance, GLFWwindow* window)
{
	VkSurfaceKHR surface = nullptr;
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
		throw GardenError("Failed to create window surface.");
	return vk::SurfaceKHR(surface);
}

//**********************************************************************************************************************
static void getVkQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface,
	uint32& graphicsQueueFamilyIndex, uint32& transferQueueFamilyIndex, uint32& computeQueueFamilyIndex,
	uint32& graphicsQueueMaxCount, uint32& transferQueueMaxCount, uint32& computeQueueMaxCount)
{
	uint32 graphicsIndex = UINT32_MAX, transferIndex = UINT32_MAX, computeIndex = UINT32_MAX;
	auto properties = physicalDevice.getQueueFamilyProperties();

	for (uint32 i = 0; i < (uint32)properties.size(); i++)
	{
		if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics &&
			physicalDevice.getSurfaceSupportKHR(i, surface))
		{
			graphicsIndex = i;
			break;
		}
	}

	if (graphicsIndex == UINT32_MAX)
		throw GardenError("No graphics queue with present on this GPU.");

	for (uint32 i = 0; i < (uint32)properties.size(); i++)
	{
		if (properties[i].queueFlags & vk::QueueFlagBits::eTransfer && graphicsIndex != i)
		{
			transferIndex = i;
			break;
		}
	}

	if (transferIndex == UINT32_MAX)
	{
		for (uint32 i = 0; i < (uint32)properties.size(); i++)
		{
			if (properties[i].queueFlags & vk::QueueFlagBits::eTransfer)
			{
				transferIndex = i;
				break;
			}
		}

		if (transferIndex == UINT32_MAX)
			throw GardenError("No transfer queue on this GPU.");
	}

	for (uint32 i = 0; i < (uint32)properties.size(); i++)
	{
		if (properties[i].queueFlags & (vk::QueueFlagBits::eCompute | 
			vk::QueueFlagBits::eTransfer) && graphicsIndex != i && transferIndex != i)
		{
			computeIndex = i;
			break;
		}
	}

	if (computeIndex == UINT32_MAX)
	{
		for (uint32 i = 0; i < (uint32)properties.size(); i++)
		{
			if (properties[i].queueFlags & (vk::QueueFlagBits::eCompute | 
				vk::QueueFlagBits::eTransfer) && graphicsIndex != i)
			{
				computeIndex = i;
				break;
			}
		}

		if (computeIndex == UINT32_MAX)
		{
			for (uint32 i = 0; i < (uint32)properties.size(); i++)
			{
				if (properties[i].queueFlags & (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer))
				{
					computeIndex = i;
					break;
				}
			}

			if (computeIndex == UINT32_MAX)
				throw GardenError("No compute queue with transfer on this GPU.");
		}
	}

	graphicsQueueFamilyIndex = graphicsIndex;
	transferQueueFamilyIndex = transferIndex;
	computeQueueFamilyIndex = computeIndex;

	graphicsQueueMaxCount = properties[graphicsIndex].queueCount;
	transferQueueMaxCount = properties[transferIndex].queueCount;
	computeQueueMaxCount = properties[computeIndex].queueCount;
}

//**********************************************************************************************************************
static vk::Device createVkDevice(vk::Instance instance, vk::PhysicalDevice physicalDevice, 
	uint32 versionMajor, uint32 versionMinor, uint32 graphicsQueueFamilyIndex, uint32 transferQueueFamilyIndex, 
	uint32 computeQueueFamilyIndex, uint32 graphicsQueueMaxCount, uint32 transferQueueMaxCount, 
	uint32 computeQueueMaxCount, uint32& frameQueueIndex, uint32& graphicsQueueIndex,
	uint32& transferQueueIndex, uint32& computeQueueIndex, VulkanAPI::Features& features)
{
	uint32 graphicsQueueCount = 1, transferQueueCount = 0, computeQueueCount = 0;
	frameQueueIndex = 0;

	if (graphicsQueueCount < graphicsQueueMaxCount)
		graphicsQueueIndex = graphicsQueueCount++;

	if (transferQueueFamilyIndex == graphicsQueueFamilyIndex)
	{
		if (graphicsQueueCount < graphicsQueueMaxCount)
			transferQueueIndex = graphicsQueueCount++;
		else
			transferQueueIndex = graphicsQueueIndex;
	}
	else
	{
		transferQueueCount = 1;
		transferQueueIndex = 0;
	}

	if (computeQueueFamilyIndex == graphicsQueueFamilyIndex)
	{
		if (graphicsQueueCount < graphicsQueueMaxCount)
			computeQueueIndex = graphicsQueueCount++;
		else
			computeQueueIndex = graphicsQueueIndex;
	}
	else if (computeQueueFamilyIndex == transferQueueFamilyIndex)
	{
		if (transferQueueCount < transferQueueMaxCount)
			computeQueueIndex = transferQueueCount++;
		else
			computeQueueIndex = transferQueueCount;
	}
	else
	{
		computeQueueCount = 1;
		computeQueueIndex = 0;
	}

	vector<float> graphicsQueuePriorities(graphicsQueueCount, 1.0f);
	vector<vk::DeviceQueueCreateInfo> queueInfos =
	{
		vk::DeviceQueueCreateInfo({}, graphicsQueueFamilyIndex, 
			graphicsQueueCount, graphicsQueuePriorities.data())
	};

	vector<float> transferQueuePriorities(graphicsQueueCount, 0.9f);
	if (transferQueueCount > 0)
	{
		queueInfos.push_back(vk::DeviceQueueCreateInfo({}, transferQueueFamilyIndex, 
			transferQueueCount, transferQueuePriorities.data()));
	}

	vector<float> computeQueuePriorities(graphicsQueueCount, 1.0f);
	if (computeQueueCount > 0)
	{
		queueInfos.push_back(vk::DeviceQueueCreateInfo({}, computeQueueFamilyIndex, 
			computeQueueCount, computeQueuePriorities.data()));
	}
	
	vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		#if GARDEN_OS_MACOS
		VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
		#endif
	};

	auto extensionProperties = physicalDevice.enumerateDeviceExtensionProperties();
	auto hasDeferredHostOperations = false, hasAccelerationStructure = false;
	auto hasDemoteToHelperInv = false;

	for (const auto& properties : extensionProperties)
	{
		auto extensionName = string_view(properties.extensionName);
		if (extensionName == VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)
			features.memoryBudget = true;
		else if (extensionName == VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME)
			features.memoryPriority = true;
		else if (extensionName == VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME)
			features.pageableMemory = true;
		else if (extensionName == VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
			hasDeferredHostOperations = true;
		else if (extensionName == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
			hasAccelerationStructure = true;
		else if (extensionName == VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME)
			hasDemoteToHelperInv = true;
		else if (extensionName == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
			features.rayTracing = true;
		else if (extensionName == VK_KHR_RAY_QUERY_EXTENSION_NAME)
			features.rayQuery = true;
		else if (extensionName == VK_AMD_ANTI_LAG_EXTENSION_NAME)
			features.amdAntiLag = true;
		else if (extensionName == VK_NV_LOW_LATENCY_2_EXTENSION_NAME)
			features.nvLowLatency = true; 

		if (versionMinor < 3)
		{
			if (extensionName == VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
				features.dynamicRendering = true;
			else if (extensionName == VK_KHR_MAINTENANCE_4_EXTENSION_NAME)
				features.maintenance4 = true;
		}
		if (versionMinor < 4)
		{
			if (extensionName == VK_KHR_MAINTENANCE_5_EXTENSION_NAME)
				features.maintenance5 = true;
			else if (extensionName == VK_KHR_MAINTENANCE_6_EXTENSION_NAME)
				features.maintenance6 = true;
		}
	}

	struct VkFeatures final
	{
		vk::PhysicalDeviceFeatures2 device;
		vk::PhysicalDeviceMaintenance4Features maintenance4;
		vk::PhysicalDeviceMaintenance5Features maintenance5;
		vk::PhysicalDeviceMaintenance6Features maintenance6;
		vk::PhysicalDevice16BitStorageFeatures _16BitStorage;
		vk::PhysicalDevice8BitStorageFeatures _8BitStorage;
		vk::PhysicalDeviceShaderFloat16Int8FeaturesKHR float16Int8;
		vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexing;
		vk::PhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayout;
		vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress;
		vk::PhysicalDeviceTimelineSemaphoreFeatures timelineSemaphore;
		vk::PhysicalDeviceVulkanMemoryModelFeatures vulkanMemoryModel;
		vk::PhysicalDeviceDynamicRenderingFeatures dynamicRendering;
		vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableMemory;
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure;
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline;
		vk::PhysicalDeviceRayQueryFeaturesKHR rayQuery;
		vk::PhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demoteToHelper;
		vk::PhysicalDeviceAntiLagFeaturesAMD amdAntiLag;
		#if GARDEN_OS_MACOS
		vk::PhysicalDevicePortabilitySubsetFeaturesKHR portability;
		#endif
	};

	auto vkFeatures = new VkFeatures();
	vkFeatures->device.pNext = &vkFeatures->_16BitStorage;
	vkFeatures->_16BitStorage.pNext = &vkFeatures->_8BitStorage;
	vkFeatures->_8BitStorage.pNext = &vkFeatures->float16Int8;
	vkFeatures->float16Int8.pNext = &vkFeatures->descriptorIndexing;
	vkFeatures->descriptorIndexing.pNext = &vkFeatures->scalarBlockLayout;
	vkFeatures->scalarBlockLayout.pNext = &vkFeatures->bufferDeviceAddress;
	vkFeatures->bufferDeviceAddress.pNext = &vkFeatures->timelineSemaphore;
	vkFeatures->timelineSemaphore.pNext = &vkFeatures->vulkanMemoryModel;
	physicalDevice.getFeatures2(&vkFeatures->device);

	if (features.memoryBudget)
		extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (features.memoryPriority)
		extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
	if (features.nvLowLatency)
		extensions.push_back(VK_NV_LOW_LATENCY_2_EXTENSION_NAME);

	if (features.pageableMemory)
	{
		vkFeatures->device.pNext = &vkFeatures->pageableMemory;
		physicalDevice.getFeatures2(&vkFeatures->device);
		if (vkFeatures->pageableMemory.pageableDeviceLocalMemory)
			extensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
		else features.pageableMemory = false;
	}

	if (versionMinor < 3)
	{
		if (features.dynamicRendering)
		{
			vkFeatures->device.pNext = &vkFeatures->dynamicRendering;
			physicalDevice.getFeatures2(&vkFeatures->device);
			if (vkFeatures->dynamicRendering.dynamicRendering)
				extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			else features.dynamicRendering = false;
		}
		if (features.maintenance4)
		{
			vkFeatures->device.pNext = &vkFeatures->maintenance4;
			physicalDevice.getFeatures2(&vkFeatures->device);
			if (vkFeatures->maintenance4.maintenance4)
				extensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
			else features.maintenance4 = false;
		}
	}
	else
	{
		features.dynamicRendering = true;
		features.maintenance4 = true;
	}

	if (versionMinor < 4)
	{
		if (features.maintenance5)
		{
			vkFeatures->device.pNext = &vkFeatures->maintenance5;
			physicalDevice.getFeatures2(&vkFeatures->device);
			if (vkFeatures->maintenance5.maintenance5)
				extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
			else features.maintenance5 = false;
		}
		if (features.maintenance6)
		{
			vkFeatures->device.pNext = &vkFeatures->maintenance6;
			physicalDevice.getFeatures2(&vkFeatures->device);
			if (vkFeatures->maintenance6.maintenance6)
				extensions.push_back(VK_KHR_MAINTENANCE_6_EXTENSION_NAME);
			else features.maintenance6 = false;
		}
	}
	else
	{
		features.maintenance5 = features.maintenance6 = true;
	}

	if (hasDeferredHostOperations && hasAccelerationStructure)
	{
		vkFeatures->device.pNext = &vkFeatures->accelerationStructure;
		physicalDevice.getFeatures2(&vkFeatures->device);

		if (vkFeatures->accelerationStructure.accelerationStructure)
		{
			extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
			extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

			if (features.rayTracing)
			{
				vkFeatures->device.pNext = &vkFeatures->rayTracingPipeline;
				physicalDevice.getFeatures2(&vkFeatures->device);
				if (vkFeatures->rayTracingPipeline.rayTracingPipeline)
					extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
				else features.rayTracing = false;
			}
			if (features.rayQuery)
			{
				vkFeatures->device.pNext = &vkFeatures->rayQuery;
				physicalDevice.getFeatures2(&vkFeatures->device);
				if (vkFeatures->rayQuery.rayQuery)
					extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
				else features.rayQuery = false;
			}
		}
		else
		{
			features.rayTracing = features.rayQuery = false;
		}
	}
	else
	{
		features.rayTracing = features.rayQuery = false;
	}

	if (hasDemoteToHelperInv)
	{
		vkFeatures->device.pNext = &vkFeatures->demoteToHelper;
		physicalDevice.getFeatures2(&vkFeatures->device);

		if (vkFeatures->demoteToHelper.shaderDemoteToHelperInvocation)
			extensions.push_back(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
		else hasDemoteToHelperInv = false;
	}

	#if GARDEN_NVIDIA_DLSS
	if (features.nvidiaDlss)
	{
		auto dlssDiscoveryInfo = getDlssDiscoveryInfo();
		uint32_t dlssExtensionCount; VkExtensionProperties* dlssExtensions;
		auto ngxResult = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, 
			physicalDevice, &dlssDiscoveryInfo, &dlssExtensionCount, &dlssExtensions);
		if (ngxResult != NVSDK_NGX_Result_Success)
			features.nvidiaDlss = false;

		if (features.nvidiaDlss)
		{
			for (uint32 i = 0; i < dlssExtensionCount; i++)
			{
				auto hasNvidiaDlss = false;
				for	(const auto& properties : extensionProperties)
				{
					if (string_view(properties.extensionName) != dlssExtensions[i].extensionName)
						continue;
					hasNvidiaDlss = true;
					break;
				}

				if (hasNvidiaDlss)
					continue;
				features.nvidiaDlss = false;
				break;
			}

			if (features.nvidiaDlss)
			{
				for (uint32 i = 0; i < dlssExtensionCount; i++)
				{
					if (!hasExtension(extensions, dlssExtensions[i].extensionName))
						extensions.push_back(dlssExtensions[i].extensionName);
				}
			}
		}
	}
	#endif

	if (features.amdAntiLag)
	{
		vkFeatures->device.pNext = &vkFeatures->amdAntiLag;
		physicalDevice.getFeatures2(&vkFeatures->device);
		if (vkFeatures->amdAntiLag.antiLag)
			extensions.push_back(VK_AMD_ANTI_LAG_EXTENSION_NAME);
		else features.amdAntiLag = false;
	}

	void** lastPNext = &vkFeatures->device.pNext;
	*lastPNext = &vkFeatures->_16BitStorage;
	lastPNext = &vkFeatures->_16BitStorage.pNext;
	*lastPNext = &vkFeatures->_8BitStorage;
	lastPNext = &vkFeatures->_8BitStorage.pNext;
	*lastPNext = &vkFeatures->float16Int8;
	lastPNext = &vkFeatures->float16Int8.pNext;
	*lastPNext = &vkFeatures->descriptorIndexing;
	lastPNext = &vkFeatures->descriptorIndexing.pNext;
	*lastPNext = &vkFeatures->scalarBlockLayout;
	lastPNext = &vkFeatures->scalarBlockLayout.pNext;
	*lastPNext = &vkFeatures->bufferDeviceAddress;
	lastPNext = &vkFeatures->bufferDeviceAddress.pNext;
	*lastPNext = &vkFeatures->timelineSemaphore;
	lastPNext = &vkFeatures->timelineSemaphore.pNext;
	*lastPNext = &vkFeatures->vulkanMemoryModel;
	lastPNext = &vkFeatures->vulkanMemoryModel.pNext;

	#if GARDEN_OS_MACOS
	vkFeatures->portability.mutableComparisonSamplers = VK_TRUE;
	*lastPNext = &vkFeatures->portability;
	lastPNext = &vkFeatures->portability.pNext;
	#endif
	*lastPNext = nullptr;

	if (features.maintenance4)
	{
		vkFeatures->maintenance4 = vk::PhysicalDeviceMaintenance4FeaturesKHR();
		vkFeatures->maintenance4.maintenance4 = VK_TRUE;
		*lastPNext = &vkFeatures->maintenance4;
		lastPNext = &vkFeatures->maintenance4.pNext;
	}
	if (features.maintenance5 && features.dynamicRendering) // Note: dynamicRendering required.
	{
		vkFeatures->maintenance5 = vk::PhysicalDeviceMaintenance5FeaturesKHR();
		vkFeatures->maintenance5.maintenance5 = VK_TRUE;
		*lastPNext = &vkFeatures->maintenance5;
		lastPNext = &vkFeatures->maintenance5.pNext;
	}
	if (features.maintenance6)
	{
		vkFeatures->maintenance6 = vk::PhysicalDeviceMaintenance6FeaturesKHR();
		vkFeatures->maintenance6.maintenance6 = VK_TRUE;
		*lastPNext = &vkFeatures->maintenance6;
		lastPNext = &vkFeatures->maintenance6.pNext;
	}
	if (features.pageableMemory)
	{
		vkFeatures->pageableMemory = vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT();
		vkFeatures->pageableMemory.pageableDeviceLocalMemory = VK_TRUE;
		*lastPNext = &vkFeatures->pageableMemory;
		lastPNext = &vkFeatures->pageableMemory.pNext;
	}
	if (features.dynamicRendering)
	{
		vkFeatures->dynamicRendering = vk::PhysicalDeviceDynamicRenderingFeatures();
		vkFeatures->dynamicRendering.dynamicRendering = VK_TRUE;
		*lastPNext = &vkFeatures->dynamicRendering;
		lastPNext = &vkFeatures->dynamicRendering.pNext;
	}
	if (features.rayTracing || features.rayQuery)
	{
		vkFeatures->accelerationStructure = vk::PhysicalDeviceAccelerationStructureFeaturesKHR();
		vkFeatures->accelerationStructure.accelerationStructure = VK_TRUE;
		*lastPNext = &vkFeatures->accelerationStructure;
		lastPNext = &vkFeatures->accelerationStructure.pNext;
	}
	if (features.rayTracing)
	{
		vkFeatures->rayTracingPipeline = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR();
		vkFeatures->rayTracingPipeline.rayTracingPipeline = VK_TRUE;
		*lastPNext = &vkFeatures->rayTracingPipeline;
		lastPNext = &vkFeatures->rayTracingPipeline.pNext;
	}
	if (features.rayQuery)
	{
		vkFeatures->rayQuery = vk::PhysicalDeviceRayQueryFeaturesKHR();
		vkFeatures->rayQuery.rayQuery = VK_TRUE;
		*lastPNext = &vkFeatures->rayQuery;
		lastPNext = &vkFeatures->rayQuery.pNext;
	}
	if (hasDemoteToHelperInv)
	{
		vkFeatures->demoteToHelper = vk::PhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT();
		vkFeatures->demoteToHelper.shaderDemoteToHelperInvocation = VK_TRUE;
		*lastPNext = &vkFeatures->demoteToHelper;
		lastPNext = &vkFeatures->demoteToHelper.pNext;
	}
	if (features.amdAntiLag)
	{
		vkFeatures->amdAntiLag = vk::PhysicalDeviceAntiLagFeaturesAMD();
		vkFeatures->amdAntiLag.antiLag = VK_TRUE;
		*lastPNext = &vkFeatures->amdAntiLag;
		lastPNext = &vkFeatures->amdAntiLag.pNext;
	}

	#if 0 // Debug only
	vk::PhysicalDeviceRayTracingValidationFeaturesNV rayTracingValidationFeatures;
	rayTracingValidationFeatures.rayTracingValidation = VK_TRUE;
	*lastPNext = &rayTracingValidationFeatures;
	lastPNext = &rayTracingValidationFeatures.pNext;
	#endif

	vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, extensions, {}, &vkFeatures->device);
	auto device = physicalDevice.createDevice(deviceInfo);
	volkLoadDevice(device);

	delete vkFeatures;
	return device;
}

//**********************************************************************************************************************
static VmaAllocator createVmaMemoryAllocator(uint32 majorVersion, uint32 minorVersion, vk::Instance instance,
	vk::PhysicalDevice physicalDevice, vk::Device device, const VulkanAPI::Features& features)
{
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.vulkanApiVersion = VK_MAKE_API_VERSION(0, majorVersion, minorVersion, 0);
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;

	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	if (features.memoryBudget)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (features.memoryPriority)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
	if (features.maintenance4)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
	if (features.maintenance5)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;

	VmaVulkanFunctions vulkanFunctions;
	auto result = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
	if (result != VK_SUCCESS)
		throw GardenError("Failed to import Vulkan function from Volk.");
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;

	VmaAllocator allocator = nullptr;
	result = vmaCreateAllocator(&allocatorInfo, &allocator);
	if (result != VK_SUCCESS)
		throw GardenError("Failed to create Vulkan memory allocator.");
	return allocator;
}

static vk::CommandPool createVkCommandPool(vk::Device device, uint32 queueFamilyIndex)
{
	vk::CommandPoolCreateInfo commandPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
	return device.createCommandPool(commandPoolInfo);
}

static vk::DescriptorPool createVkDescriptorPool(vk::Device device, const VulkanAPI::Features& features)
{
	vector<vk::DescriptorPoolSize> sizes;
	if (GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
			GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT));
	}
	if (GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
			GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT));
	}
	if (GARDEN_DS_POOL_STORAGE_IMAGE_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage,
			GARDEN_DS_POOL_STORAGE_IMAGE_COUNT));
	}
	if (GARDEN_DS_POOL_STORAGE_BUFFER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer,
			GARDEN_DS_POOL_STORAGE_BUFFER_COUNT));
	}
	if (GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment,
			GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT));
	}
	if (GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT > 0 && features.rayTracing)
	{
		sizes.push_back(vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR,
			GARDEN_DS_POOL_ACCEL_STRUCTURE_COUNT));
	}
	
	uint32 maxSetCount = 0;
	for (auto& size : sizes)
		maxSetCount += size.descriptorCount;

	vk::DescriptorPoolCreateInfo descriptorPoolInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxSetCount, (uint32)sizes.size(), sizes.data());
	return device.createDescriptorPool(descriptorPoolInfo);
}

//**********************************************************************************************************************
namespace garden::graphics
{
	struct PipelineCacheHeader final
	{
		char							magic[4];
		uint32							engineVersion;
		uint32							appVersion;
		uint32							dataSize;
		Hash128							dataHash;
		uint32	 						driverVersion;
		uint32	 						driverABI;
		VkPipelineCacheHeaderVersionOne	cache;
	};
}

static vk::PipelineCache createPipelineCache(const string& appDataName, Version appVersion,
	vk::Device device, const vk::PhysicalDeviceProperties2& deviceProperties, bool& isLoaded)
{
	constexpr auto cacheHeaderSize = sizeof(PipelineCacheHeader) - sizeof(VkPipelineCacheHeaderVersionOne);
	auto path = mpio::Directory::getAppDataPath(appDataName) / "cache/shaders";
	ifstream inputStream(path, ios::in | ios::binary | ios::ate);
	vector<uint8> fileData;

	vk::PipelineCacheCreateInfo cacheInfo;
	if (inputStream.is_open())
	{
		auto fileSize = (psize)inputStream.tellg();
		if (fileSize > sizeof(PipelineCacheHeader))
		{
			fileData.resize(fileSize);
			inputStream.seekg(0, ios::beg);

			if (inputStream.read((char*)fileData.data(), fileSize))
			{
				PipelineCacheHeader targetHeader;
				memcpy(targetHeader.magic, "GSLC", 4);
				targetHeader.engineVersion = VK_MAKE_API_VERSION(0, GARDEN_VERSION_MAJOR,
					GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
				targetHeader.appVersion = VK_MAKE_API_VERSION(0,
					appVersion.major, appVersion.minor, appVersion.patch);
				targetHeader.dataSize = (uint32)(fileSize - cacheHeaderSize);
				targetHeader.dataHash = Hash128(fileData.data() + cacheHeaderSize,
					(psize)fileSize - cacheHeaderSize);
				targetHeader.driverVersion = deviceProperties.properties.driverVersion;
				targetHeader.driverABI = sizeof(void*);
				targetHeader.cache.headerSize = sizeof(VkPipelineCacheHeaderVersionOne);
				targetHeader.cache.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
				targetHeader.cache.vendorID = deviceProperties.properties.vendorID;
				targetHeader.cache.deviceID = deviceProperties.properties.deviceID;
				memcpy(targetHeader.cache.pipelineCacheUUID,
					deviceProperties.properties.pipelineCacheUUID, VK_UUID_SIZE);

				if (memcmp(fileData.data(), &targetHeader, sizeof(PipelineCacheHeader)) == 0)
				{
					cacheInfo.initialDataSize = fileData.size() - cacheHeaderSize;
					cacheInfo.pInitialData = fileData.data() + cacheHeaderSize;
					isLoaded = true;
				}
				else isLoaded = false;
			}
		}
	}

	return device.createPipelineCache(cacheInfo);
}

//**********************************************************************************************************************
VulkanAPI::VulkanAPI(const string& appName, const string& appDataName, Version appVersion, uint2 windowSize, 
	int32 threadCount, bool useVsync, bool useTripleBuffering, bool isFullscreen, bool isDecorated) : 
	GraphicsAPI(appName, windowSize, isFullscreen, isDecorated)
{
	this->backendType = GraphicsBackend::VulkanAPI;
	this->threadCount = threadCount;
	this->appDataName = appDataName;
	this->appVersion = appVersion;
	this->currentPipelines.resize(threadCount);
	this->currentPipelineTypes.resize(threadCount);
	this->currentPipelineVariants.resize(threadCount);
	this->currentVertexBuffers.resize(threadCount);
	this->currentIndexBuffers.resize(threadCount);
	this->bindDescriptorSets.resize(threadCount);

	uint32 graphicsQueueMaxCount = 0, transferQueueMaxCount = 0, computeQueueMaxCount = 0;
	uint32 frameQueueIndex = 0, graphicsQueueIndex = 0, transferQueueIndex = 0, computeQueueIndex = 0;

	if (volkInitialize() != VK_SUCCESS)
		throw GardenError("Failed to load Vulkan loader.");

	instance = createVkInstance(appName, appVersion, versionMajor, versionMinor, features);

	#if GARDEN_DEBUG
	if (features.debugUtils)
		debugMessenger = createVkDebugMessenger(instance);
	#endif
	
	physicalDevice = getBestPhysicalDevice(instance);
	deviceProperties = physicalDevice.getProperties2();
	deviceFeatures = physicalDevice.getFeatures2();
	versionMajor = VK_API_VERSION_MAJOR(deviceProperties.properties.apiVersion);
	versionMinor = VK_API_VERSION_MINOR(deviceProperties.properties.apiVersion);
	isDeviceIntegrated = deviceProperties.properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
	surface = createVkSurface(instance, (GLFWwindow*)window);
	getVkQueueFamilyIndices(physicalDevice, surface, graphicsQueueFamilyIndex, transferQueueFamilyIndex, 
		computeQueueFamilyIndex, graphicsQueueMaxCount, transferQueueMaxCount, computeQueueMaxCount);
	device = createVkDevice(instance, physicalDevice, versionMajor, versionMinor, graphicsQueueFamilyIndex, 
		transferQueueFamilyIndex, computeQueueFamilyIndex, graphicsQueueMaxCount, transferQueueMaxCount, 
		computeQueueMaxCount, frameQueueIndex, graphicsQueueIndex, transferQueueIndex, computeQueueIndex, features);
	memoryAllocator = createVmaMemoryAllocator(versionMajor, versionMinor, instance, physicalDevice, device, features);
	frameQueue = device.getQueue(graphicsQueueFamilyIndex, frameQueueIndex);
	graphicsQueue = device.getQueue(graphicsQueueFamilyIndex, graphicsQueueIndex);
	transferQueue = device.getQueue(transferQueueFamilyIndex, transferQueueIndex);
	computeQueue = device.getQueue(computeQueueFamilyIndex, computeQueueIndex);
	frameCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	graphicsCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	transferCommandPool = createVkCommandPool(device, transferQueueFamilyIndex);
	computeCommandPool = createVkCommandPool(device, computeQueueFamilyIndex);
	descriptorPool = createVkDescriptorPool(device, features);
	pipelineCache = createPipelineCache(appDataName, appVersion, device, deviceProperties, isCacheLoaded);

	if (features.nvLowLatency)
	{
		vk::SemaphoreTypeCreateInfo typeInfo(vk::SemaphoreType::eTimeline, 0);
		vk::SemaphoreCreateInfo semaphoreInfo({}, &typeInfo);
		pacingSemaphore = device.createSemaphore(semaphoreInfo);
	}
	if (features.rayTracing)
	{
		deviceProperties.pNext = &rtProperties;
		rtProperties.pNext = &asProperties;
		physicalDevice.getProperties2(&deviceProperties);
	}

	switch (deviceProperties.properties.vendorID)
	{
		case 0x10DE: gpuVendor = GpuVendor::Nvidia; break;
		case 0x1002: gpuVendor = GpuVendor::AMD; break;
		case 0x8086: gpuVendor = GpuVendor::Intel; break;
		case 0x106B: gpuVendor = GpuVendor::Apple; break;
		case 0x13B5: gpuVendor = GpuVendor::ARM; break;
		case 0x5143: gpuVendor = GpuVendor::Qualcomm; break;
		case 0x1010: gpuVendor = GpuVendor::ImgTec; break;
	}

	int sizeX = 0, sizeY = 0;
	glfwGetFramebufferSize((GLFWwindow*)window, &sizeX, &sizeY);
	swapchain = vulkanSwapchain = new VulkanSwapchain(this, uint2(sizeX, sizeY), useVsync, useTripleBuffering);

	frameCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::Frame);
	graphicsCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::Graphics);
	transferCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::TransferOnly);
	computeCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::Compute);

	GARDEN_ASSERT_MSG(!vulkanInstance, "Graphics API is already initialized");
	vulkanInstance = this;
}

//**********************************************************************************************************************
VulkanAPI::~VulkanAPI()
{
	// Note: Should be set here, to destroy resources.
	forceResourceDestroy = false;

	for (auto secondaryCommandState : secondaryCommandStates)
		delete secondaryCommandState;

	delete computeCommandBuffer;
	delete transferCommandBuffer;
	delete graphicsCommandBuffer;
	delete frameCommandBuffer;

	for (int i = 0; i < inFlightCount + 1; i++)
		flushDestroyBuffer();
	delete swapchain;

	tlasPool.clear();
	blasPool.clear();
	descriptorSetPool.clear();
	rayTracingPipelinePool.clear();
	computePipelinePool.clear();
	graphicsPipelinePool.clear();
	samplerPool.clear();
	framebufferPool.clear();
	renderPasses.clear();
	imageViewPool.clear();
	imagePool.clear();
	bufferPool.clear();

	if (device)
	{
		device.destroySemaphore(pacingSemaphore);
		device.destroyPipelineCache(pipelineCache);
		device.destroyDescriptorPool(descriptorPool);
		device.destroyCommandPool(computeCommandPool);
		device.destroyCommandPool(transferCommandPool);
		device.destroyCommandPool(graphicsCommandPool);
		device.destroyCommandPool(frameCommandPool);
		vmaDestroyAllocator(memoryAllocator);
		device.destroy();
	}
	
	instance.destroySurfaceKHR(surface);

	#if GARDEN_DEBUG
	if (features.debugUtils)
		instance.destroy(debugMessenger, nullptr);
	#endif

	instance.destroy();
	volkFinalize();

	GARDEN_ASSERT_MSG(vulkanInstance, "Graphics API is not initialized");
	vulkanInstance = nullptr;
}

//**********************************************************************************************************************
void VulkanAPI::flushDestroyBuffer()
{
	auto& destroyBuffer = destroyBuffers[flushDestroyIndex];
	flushDestroyIndex = (flushDestroyIndex + 1) % (inFlightCount + 1);
	fillDestroyIndex = (fillDestroyIndex + 1) % (inFlightCount + 1);

	if (destroyBuffer.empty())
		return;

	sort(destroyBuffer.begin(), destroyBuffer.end(), [](const GraphicsAPI::DestroyResource& a,
		const GraphicsAPI::DestroyResource& b) { return (uint32)a.type < (uint32)b.type; });

	for (auto& resource : destroyBuffer)
	{
		switch (resource.type)
		{
		case GraphicsAPI::DestroyResourceType::DescriptorSet:
			if (resource.count > 0)
			{
				auto result = device.freeDescriptorSets(descriptorPool,
					resource.count, (vk::DescriptorSet*)resource.data0);
				vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
				free(resource.data0);
			}
			else
			{
				auto result = device.freeDescriptorSets(descriptorPool, 1, (vk::DescriptorSet*)&resource.data0);
				vk::detail::resultCheck(result, "vk::Device::freeDescriptorSets");
			}
			break;
		case GraphicsAPI::DestroyResourceType::Pipeline:
			if (resource.count > 0)
			{
				for (uint32 i = 0; i < resource.count; i++)
					device.destroyPipeline(((VkPipeline*)resource.data0)[i]);
				free(resource.data0);
			}
			else
			{
				device.destroyPipeline((VkPipeline)resource.data0);
			}
			device.destroyPipelineLayout((VkPipelineLayout)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::DescriptorPool:
			device.destroyDescriptorPool((VkDescriptorPool)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::DescriptorSetLayout:
			device.destroyDescriptorSetLayout((VkDescriptorSetLayout)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Sampler:
			device.destroySampler((VkSampler)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Framebuffer:
			device.destroyFramebuffer((VkFramebuffer)resource.data0);
			device.destroyRenderPass((VkRenderPass)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::ImageView:
			device.destroyImageView((VkImageView)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Image:
			vmaDestroyImage(memoryAllocator, (VkImage)resource.data0, (VmaAllocation)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::Tlas:
		case GraphicsAPI::DestroyResourceType::Blas:
			device.destroyAccelerationStructureKHR((VkAccelerationStructureKHR)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Buffer:
			vmaDestroyBuffer(memoryAllocator, (VkBuffer)resource.data0, (VmaAllocation)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::QueryPool:
			device.destroyQueryPool((VkQueryPool)resource.data0);
			break;
		default: abort();
		}
	}

	destroyBuffer.clear();
}

void VulkanAPI::storePipelineCache()
{
	auto cacheData = device.getPipelineCacheData((VkPipelineCache)pipelineCache);
	if (cacheData.size() > sizeof(VkPipelineCacheHeaderVersionOne))
	{
		auto directory = mpio::Directory::getAppDataPath(appDataName) / "cache";
		if (!fs::exists(directory))
			fs::create_directories(directory);
		auto path = directory / "shaders";
		ofstream outputStream(path, ios::out | ios::binary);

		if (outputStream.is_open())
		{
			outputStream.write("GSLC", 4);
			constexpr uint32 vkEngineVersion = VK_MAKE_API_VERSION(0, 
				GARDEN_VERSION_MAJOR, GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
			const uint32 vkAppVersion = VK_MAKE_API_VERSION(0,
				appVersion.major, appVersion.minor, appVersion.patch);
			outputStream.write((const char*)&vkEngineVersion, sizeof(uint32));
			outputStream.write((const char*)&vkAppVersion, sizeof(uint32));
			auto dataSize = (uint32)cacheData.size();
			outputStream.write((const char*)&dataSize, sizeof(uint32));
			auto hash = Hash128(cacheData.data(), cacheData.size());
			outputStream.write((const char*)&hash, sizeof(Hash128));
			outputStream.write((const char*)
				&deviceProperties.properties.driverVersion, sizeof(uint32));
			auto driverABI = (uint32)sizeof(void*);
			outputStream.write((const char*)&driverABI, sizeof(uint32));
			outputStream.write((const char*)cacheData.data(), cacheData.size());
		}
	}
}

void VulkanAPI::waitIdle()
{
	device.waitIdle();
}