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

#include "garden/graphics/vulkan/api.hpp"
#include "garden/graphics/vulkan/command-buffer.hpp"
#include "garden/hash.hpp"

#define VMA_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#include "garden/graphics/vulkan/vma.hpp"
#include "garden/graphics/glfw.hpp" // Do not move it.

#include "mpio/directory.hpp"

#include <vector>
#include <fstream>
#include <iostream>

using namespace garden;

#if GARDEN_DEBUG
//**********************************************************************************************************************
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
	// TODO: investigate this error after driver/SDK updates.
	if (callbackData->messageIdNumber == -1254218959 || callbackData->messageIdNumber == -2080204129 ||
		callbackData->messageIdNumber == 774851941)
	{
		return VK_FALSE;
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
	cout << "VULKAN::" << severity << ": ";

	for (int32 i = callbackData->cmdBufLabelCount - 1; i >= 0; i--)
		cout << callbackData->pCmdBufLabels[i].pLabelName << " -> ";
	cout << callbackData->pMessage << "\n\n";

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

//**********************************************************************************************************************
static vk::Instance createVkInstance(const string& appName, Version appVersion,
	uint32& instanceVersionMajor, uint32& instanceVersionMinor
	#if GARDEN_DEBUG
	, bool& hasDebugUtils
	#endif
	)
{
	auto getInstanceVersion = (PFN_vkEnumerateInstanceVersion)
		vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
	if (!getInstanceVersion)
		throw GardenError("Vulkan API 1.0 is not supported.");

	uint32 installedVersion = 0;
	auto vkResult = (vk::Result)getInstanceVersion(&installedVersion);
	if (vkResult != vk::Result::eSuccess)
		throw GardenError("Failed to get Vulkan version.");

	instanceVersionMajor = VK_API_VERSION_MAJOR(installedVersion);
	instanceVersionMinor = VK_API_VERSION_MINOR(installedVersion);
	// instanceVersionMinor = 2; // TODO: debugging

	#if GARDEN_OS_MACOS
	// TODO: remove after MoltenVK 1.3 support on mac.
	if (instanceVersionMinor >= 3)
		instanceVersionMinor = 2;
	#endif

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
	extensions.resize(glfwExtensionCount);

	for (uint32 i = 0; i < glfwExtensionCount; i++)
		extensions[i] = glfwExtensions[i];

	#if GARDEN_OS_MACOS
	if (!hasExtension(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	#endif

	auto extensionProperties = vk::enumerateInstanceExtensionProperties();
	auto layerProperties = vk::enumerateInstanceLayerProperties();
	const void* instanceInfoNext = nullptr;

	#if GARDEN_DEBUG

	#if GARDEN_GAPI_VALIDATIONS
	for	(const auto& properties : layerProperties)
	{
		if (strcmp(properties.layerName.data(), "VK_LAYER_KHRONOS_validation") == 0)
		{
			layers.push_back("VK_LAYER_KHRONOS_validation");
			break;
		}
	}
	#endif

	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo;
	hasDebugUtils = false;
	for	(const auto& properties : extensionProperties)
	{
		if (strcmp(properties.extensionName.data(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			debugUtilsInfo.messageSeverity = debugMessageSeverity;
			debugUtilsInfo.messageType = debugMessageType;
			debugUtilsInfo.pfnUserCallback = vkDebugMessengerCallback;
			instanceInfoNext = &debugUtilsInfo;
			hasDebugUtils = true;
			break;
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
		throw GardenError("No Vulkan graphics queue with present.");

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
			throw GardenError("No Vulkan transfer queue.");
	}

	for (uint32 i = 0; i < (uint32)properties.size(); i++)
	{
		if (properties[i].queueFlags & vk::QueueFlagBits::eCompute &&
			graphicsIndex != i && transferIndex != i)
		{
			computeIndex = i;
			break;
		}
	}

	if (computeIndex == UINT32_MAX)
	{
		for (uint32 i = 0; i < (uint32)properties.size(); i++)
		{
			if (properties[i].queueFlags & vk::QueueFlagBits::eCompute && graphicsIndex != i)
			{
				computeIndex = i;
				break;
			}
		}

		if (computeIndex == UINT32_MAX)
		{
			for (uint32 i = 0; i < (uint32)properties.size(); i++)
			{
				if (properties[i].queueFlags & vk::QueueFlagBits::eCompute)
				{
					computeIndex = i;
					break;
				}
			}

			if (computeIndex == UINT32_MAX)
				throw GardenError("No Vulkan compute queue.");
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
static vk::Device createVkDevice(vk::PhysicalDevice physicalDevice, uint32 versionMajor, uint32 versionMinor,
	uint32 graphicsQueueFamilyIndex, uint32 transferQueueFamilyIndex, uint32 computeQueueFamilyIndex, uint32 graphicsQueueMaxCount,
	uint32 transferQueueMaxCount, uint32 computeQueueMaxCount, uint32& frameQueueIndex, uint32& graphicsQueueIndex,
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
	auto hasDeferredHostOperations = false, hasAccelerationStructure = false, hasRayTracingPipeline = false;

	for (const auto& properties : extensionProperties)
	{
		if (strcmp(properties.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
			features.memoryBudget = true;
		else if (strcmp(properties.extensionName, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) == 0)
			features.memoryPriority = true;
		else if (strcmp(properties.extensionName, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME) == 0)
			features.pageableMemory = true;
		else if (strcmp(properties.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
			hasDeferredHostOperations = true;
		else if (strcmp(properties.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
			hasAccelerationStructure = true;
		else if (strcmp(properties.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
			hasRayTracingPipeline = true;

		if (versionMinor < 2)
		{
			if (strcmp(properties.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
				features.descriptorIndexing = true;
			else if (strcmp(properties.extensionName, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
				features.bufferDeviceAddress = true;
		}
		if (versionMinor < 3)
		{
			if (strcmp(properties.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
				features.dynamicRendering = true;
			else if (strcmp(properties.extensionName, VK_KHR_MAINTENANCE_4_EXTENSION_NAME) == 0)
				features.maintenance4 = true;
		}
		if (versionMinor < 4)
		{
			if (strcmp(properties.extensionName, VK_KHR_MAINTENANCE_5_EXTENSION_NAME) == 0)
				features.maintenance5 = true;
		}
	}

	vk::PhysicalDeviceFeatures2 deviceFeatures;
	vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableMemoryFeatures;
	vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
	vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
	vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures;
	vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;
	vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeloneFeatures;
	vk::PhysicalDeviceMaintenance4FeaturesKHR maintenance4Features;
	vk::PhysicalDeviceMaintenance5FeaturesKHR maintenance5Features;

	if (features.memoryBudget)
		extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (features.memoryPriority)
		extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);

	if (features.pageableMemory)
	{
		deviceFeatures.pNext = &pageableMemoryFeatures;
		physicalDevice.getFeatures2(&deviceFeatures);
		if (pageableMemoryFeatures.pageableDeviceLocalMemory)
			extensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
		else
			features.pageableMemory = false;
	}

	if (versionMinor < 2)
	{
		if (features.descriptorIndexing)
		{
			deviceFeatures.pNext = &descriptorIndexingFeatures;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind &&
				descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind &&
				descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind &&
				descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind &&
				descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind &&
				descriptorIndexingFeatures.descriptorBindingPartiallyBound &&
				descriptorIndexingFeatures.runtimeDescriptorArray)
			{
				extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
			}
			else
			{
				features.descriptorIndexing = false;
			}
		}
		if (features.bufferDeviceAddress)
		{
			deviceFeatures.pNext = &bufferDeviceAddressFeatures;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (bufferDeviceAddressFeatures.bufferDeviceAddress)
				extensions.push_back(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
			else
				features.bufferDeviceAddress = false;
		}
	}
	else
	{
		features.descriptorIndexing = true;
		features.bufferDeviceAddress = true;
	}

	if (versionMinor < 3)
	{
		if (features.dynamicRendering)
		{
			deviceFeatures.pNext = &dynamicRenderingFeatures;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (dynamicRenderingFeatures.dynamicRendering)
				extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			else
				features.dynamicRendering = false;
		}
		if (features.maintenance4)
		{
			deviceFeatures.pNext = &maintenance4Features;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (maintenance4Features.maintenance4)
				extensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
			else
				features.maintenance4 = false;
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
			deviceFeatures.pNext = &maintenance5Features;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (maintenance5Features.maintenance5)
				extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
			else
				features.maintenance5 = false;
		}
	}
	else
	{
		features.maintenance5 = true;
	}

	if (hasDeferredHostOperations && hasAccelerationStructure && hasRayTracingPipeline && 
		features.descriptorIndexing && features.bufferDeviceAddress)
	{
		deviceFeatures.pNext = &accelerationStructureFeatures;
		physicalDevice.getFeatures2(&deviceFeatures);
		deviceFeatures.pNext = &rayTracingPipeloneFeatures;
		physicalDevice.getFeatures2(&deviceFeatures);

		if (accelerationStructureFeatures.accelerationStructure && rayTracingPipeloneFeatures.rayTracingPipeline)
		{
			extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
			extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
			extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
			features.rayTracing = true;
		}
		else
		{
			features.rayTracing = false;
		}
	}

	deviceFeatures.features = vk::PhysicalDeviceFeatures();
	deviceFeatures.features.independentBlend = VK_TRUE;
	deviceFeatures.features.depthClamp = VK_TRUE;
	deviceFeatures.features.samplerAnisotropy = VK_TRUE;
	deviceFeatures.pNext = nullptr;
	void** lastPNext = &deviceFeatures.pNext;

	#if GARDEN_OS_MACOS
	vk::PhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	portabilityFeatures.mutableComparisonSamplers = VK_TRUE;
	*lastPNext = &portabilityFeatures;
	lastPNext = &portabilityFeatures.pNext;
	#endif

	if (features.maintenance4)
	{
		maintenance4Features = vk::PhysicalDeviceMaintenance4FeaturesKHR();
		maintenance4Features.maintenance4 = VK_TRUE;
		maintenance4Features.pNext = nullptr;
		*lastPNext = &maintenance4Features;
		lastPNext = &maintenance4Features.pNext;
	}
	if (features.maintenance5 && features.dynamicRendering) // dynamicRendering required
	{
		maintenance5Features = vk::PhysicalDeviceMaintenance5FeaturesKHR();
		maintenance5Features.maintenance5 = VK_TRUE;
		maintenance5Features.pNext = nullptr;
		*lastPNext = &maintenance5Features;
		lastPNext = &maintenance5Features.pNext;
	}
	if (features.pageableMemory)
	{
		pageableMemoryFeatures = vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT();
		pageableMemoryFeatures.pageableDeviceLocalMemory = VK_TRUE;
		pageableMemoryFeatures.pNext = nullptr;
		*lastPNext = &pageableMemoryFeatures;
		lastPNext = &pageableMemoryFeatures.pNext;
	}
	if (features.descriptorIndexing)
	{
		descriptorIndexingFeatures = vk::PhysicalDeviceDescriptorIndexingFeatures();
		descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
		descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
		descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
		descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
		descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
		descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
		*lastPNext = &descriptorIndexingFeatures;
		lastPNext = &descriptorIndexingFeatures.pNext;
	}
	if (features.bufferDeviceAddress)
	{
		bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures();
		bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
		*lastPNext = &bufferDeviceAddressFeatures;
		lastPNext = &bufferDeviceAddressFeatures.pNext;
	}
	if (features.dynamicRendering)
	{
		dynamicRenderingFeatures = vk::PhysicalDeviceDynamicRenderingFeatures();
		dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
		*lastPNext = &dynamicRenderingFeatures;
		lastPNext = &dynamicRenderingFeatures.pNext;
	}
	if (features.rayTracing)
	{
		accelerationStructureFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR();
		accelerationStructureFeatures.accelerationStructure = VK_TRUE;
		*lastPNext = &accelerationStructureFeatures;
		lastPNext = &accelerationStructureFeatures.pNext;

		rayTracingPipeloneFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR();
		rayTracingPipeloneFeatures.rayTracingPipeline = VK_TRUE;
		*lastPNext = &rayTracingPipeloneFeatures;
		lastPNext = &rayTracingPipeloneFeatures.pNext;
	}

	vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, extensions, {}, &deviceFeatures);
	auto device = physicalDevice.createDevice(deviceInfo);
	volkLoadDevice(device);
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

	if (features.memoryBudget)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (features.memoryPriority)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
	if (features.bufferDeviceAddress)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
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

static vk::DescriptorPool createVkDescriptorPool(vk::Device device)
{
	vector<vk::DescriptorPoolSize> sizes;
	if (GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eCombinedImageSampler,
			GARDEN_DS_POOL_COMBINED_SAMPLER_COUNT));
	}
	if (GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eUniformBuffer,
			GARDEN_DS_POOL_UNIFORM_BUFFER_COUNT));
	}
	if (GARDEN_DS_POOL_STORAGE_IMAGE_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eStorageImage,
			GARDEN_DS_POOL_STORAGE_IMAGE_COUNT));
	}
	if (GARDEN_DS_POOL_STORAGE_BUFFER_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eStorageBuffer,
			GARDEN_DS_POOL_STORAGE_BUFFER_COUNT));
	}
	if (GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT > 0)
	{
		sizes.push_back(vk::DescriptorPoolSize(
			vk::DescriptorType::eInputAttachment,
			GARDEN_DS_POOL_INPUT_ATTACHMENT_COUNT));
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
	this->currentVertexBuffers.resize(threadCount);
	this->currentIndexBuffers.resize(threadCount);
	this->bindDescriptorSets.resize(threadCount);

	uint32 graphicsQueueMaxCount = 0, transferQueueMaxCount = 0, computeQueueMaxCount = 0;
	uint32 frameQueueIndex = 0, graphicsQueueIndex = 0, transferQueueIndex = 0, computeQueueIndex = 0;

	if (volkInitialize() != VK_SUCCESS)
		throw GardenError("Failed to load Vulkan loader.");

	#if GARDEN_DEBUG
	instance = createVkInstance(appName, appVersion, versionMajor, versionMinor, hasDebugUtils);
	if (hasDebugUtils)
		debugMessenger = createVkDebugMessenger(instance);
	#else
	instance = createVkInstance(appName, appVersion, versionMajor, versionMinor);
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
	device = createVkDevice(physicalDevice, versionMajor, versionMinor, graphicsQueueFamilyIndex, 
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
	descriptorPool = createVkDescriptorPool(device);
	pipelineCache = createPipelineCache(appDataName, appVersion, device, deviceProperties, isCacheLoaded);

	if (features.rayTracing)
	{
		deviceProperties.pNext = &asProperties;
		physicalDevice.getProperties2(&deviceProperties);
	}

	int sizeX = 0, sizeY = 0;
	glfwGetFramebufferSize((GLFWwindow*)window, &sizeX, &sizeY);
	swapchain = vulkanSwapchain = new VulkanSwapchain(this, uint2(sizeX, sizeY), useVsync, useTripleBuffering);

	frameCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::Frame);
	graphicsCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::Graphics);
	transferCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::TransferOnly);
	computeCommandBuffer = new VulkanCommandBuffer(this, CommandBufferType::ComputeOnly);

	GARDEN_ASSERT(!vulkanInstance);
	vulkanInstance = this;
}

//**********************************************************************************************************************
VulkanAPI::~VulkanAPI()
{
	// Should be set here, to destroy resources.
	forceResourceDestroy = false;

	for (auto secondaryCommandState : secondaryCommandStates)
		delete secondaryCommandState;

	delete computeCommandBuffer;
	delete transferCommandBuffer;
	delete graphicsCommandBuffer;
	delete frameCommandBuffer;

	for (int i = 0; i < frameLag + 1; i++)
		flushDestroyBuffer();
	delete swapchain;

	descriptorSetPool.clear();
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
	if (hasDebugUtils)
		instance.destroy(debugMessenger, nullptr);
	#endif

	instance.destroy();

	GARDEN_ASSERT(vulkanInstance);
	vulkanInstance = nullptr;
}

//**********************************************************************************************************************
void VulkanAPI::flushDestroyBuffer()
{
	auto& destroyBuffer = destroyBuffers[flushDestroyIndex];
	flushDestroyIndex = (flushDestroyIndex + 1) % (frameLag + 1);
	fillDestroyIndex = (fillDestroyIndex + 1) % (frameLag + 1);

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
				device.freeDescriptorSets(descriptorPool,
					resource.count, (vk::DescriptorSet*)resource.data0);
				free(resource.data0);
			}
			else
			{
				device.freeDescriptorSets(descriptorPool, 1, (vk::DescriptorSet*)&resource.data0);
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
		case GraphicsAPI::DestroyResourceType::AccelerationStructure:
			device.destroyAccelerationStructureKHR((VkAccelerationStructureKHR)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Buffer:
			vmaDestroyBuffer(memoryAllocator, (VkBuffer)resource.data0, (VmaAllocation)resource.data1);
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
			const uint32 vkEngineVersion = VK_MAKE_API_VERSION(0, GARDEN_VERSION_MAJOR,
				GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
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