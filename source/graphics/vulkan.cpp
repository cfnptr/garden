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

#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/glfw.hpp"
#include "mpio/directory.hpp"

#if GARDEN_OS_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#define NOMINMAX
#include "GLFW/glfw3native.h"
#pragma comment (lib, "Dwmapi")
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <array>
#include <vector>
#include <thread>
#include <fstream>
#include <iostream>

using namespace mpio;
using namespace garden;
using namespace garden::graphics;

#if GARDEN_DEBUG
//**********************************************************************************************************************
constexpr vk::DebugUtilsMessageSeverityFlagsEXT debugMessageSeverity =
	//vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
constexpr vk::DebugUtilsMessageTypeFlagsEXT debugMessageType =
	vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
	vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
	vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding;
#endif

#if GARDEN_DEBUG
//**********************************************************************************************************************
static VkBool32 VKAPI_CALL vkDebugMessengerCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData)
{
	// TODO: investigate this error after driver/SDK updates.
	if (callbackData->messageIdNumber == -1254218959 || callbackData->messageIdNumber == -2080204129)
		return VK_FALSE;

	const char* severity;
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
		severity = "VERBOSE";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		severity = "INFO";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		severity = "WARNING";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		severity = "ERROR";
	else
		severity = "UNKNOWN";
	cout << "VULKAN::" << severity << ": " << callbackData->pMessage << "\n";

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		return VK_FALSE; // WARNING severity debugging breakpoint
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		return VK_FALSE; // ERROR severity debugging breakpoint
	return VK_FALSE;
}
#endif

static bool hasExtension(const vector<const char*>& extensions, const char* extension)
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
		throw runtime_error("Vulkan API 1.0 is not supported.");

	uint32 installedVersion = 0;
	auto vkResult = (vk::Result)getInstanceVersion(&installedVersion);
	if (vkResult != vk::Result::eSuccess)
		throw runtime_error("Failed to get Vulkan version.");

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
	return vk::createInstance(instanceInfo);
}

#if GARDEN_DEBUG
static vk::DebugUtilsMessengerEXT createVkDebugMessenger(
	vk::Instance instance, const vk::DispatchLoaderDynamic& dynamicLoader)
{
	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo;
	debugUtilsInfo.messageSeverity = debugMessageSeverity;
	debugUtilsInfo.messageType = debugMessageType;
	debugUtilsInfo.pfnUserCallback = vkDebugMessengerCallback;
	return instance.createDebugUtilsMessengerEXT(debugUtilsInfo, nullptr, dynamicLoader);
}
#endif

//**********************************************************************************************************************
static vk::PhysicalDevice getBestPhysicalDevice(vk::Instance instance)
{
	auto devices = instance.enumeratePhysicalDevices();

	if (devices.empty())
		throw runtime_error("No suitable physical device.");

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
		throw runtime_error("Failed to create window surface.");
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
		throw runtime_error("No Vulkan graphics queue with present.");

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
			throw runtime_error("No Vulkan transfer queue.");
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
				throw runtime_error("No Vulkan compute queue.");
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
static vk::Device createVkDevice(
	vk::PhysicalDevice physicalDevice, uint32 versionMajor, uint32 versionMinor,
	uint32 graphicsQueueFamilyIndex, uint32 transferQueueFamilyIndex,
	uint32 computeQueueFamilyIndex, uint32 graphicsQueueMaxCount,
	uint32 transferQueueMaxCount, uint32 computeQueueMaxCount,
	uint32& frameQueueIndex, uint32& graphicsQueueIndex,
	uint32& transferQueueIndex, uint32& computeQueueIndex,
	bool& hasMemoryBudget, bool& hasMemoryPriority, bool& hasPageableMemory,
	bool& hasDynamicRendering, bool& hasDescriptorIndexing)
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
	for (const auto& properties : extensionProperties)
	{
		if (strcmp(properties.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
			hasMemoryBudget = true;
		if (strcmp(properties.extensionName, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) == 0)
			hasMemoryPriority = true;
		if (strcmp(properties.extensionName, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME) == 0)
			hasPageableMemory = true;

		if (versionMinor < 2)
		{
			if (strcmp(properties.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
				hasDescriptorIndexing = true;
		}
		if (versionMinor < 3)
		{
			if (strcmp(properties.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
				hasDynamicRendering = true;
		}
	}

	vk::PhysicalDeviceFeatures2 deviceFeatures;
	vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableMemoryFeatures;
	vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
	vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;

	if (hasMemoryBudget)
		extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (hasMemoryPriority) 
		extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);

	if (hasPageableMemory)
	{
		deviceFeatures.pNext = &pageableMemoryFeatures;
		physicalDevice.getFeatures2(&deviceFeatures);
		if (pageableMemoryFeatures.pageableDeviceLocalMemory)
			extensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
		else
			hasPageableMemory = false;
	}

	if (versionMinor < 2)
	{
		if (hasDescriptorIndexing)
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
				hasDescriptorIndexing = false;
			}
		}
	}
	else
	{
		hasDescriptorIndexing = true;
	}
	if (versionMinor < 3)
	{
		if (hasDynamicRendering)
		{
			deviceFeatures.pNext = &dynamicRenderingFeatures;
			physicalDevice.getFeatures2(&deviceFeatures);
			if (dynamicRenderingFeatures.dynamicRendering)
				extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			else
				hasDynamicRendering = false;
		}
	}
	else
	{
		hasDynamicRendering = true;
	}

	deviceFeatures.features = vk::PhysicalDeviceFeatures();
	deviceFeatures.features.independentBlend = VK_TRUE;
	deviceFeatures.features.depthClamp = VK_TRUE;
	deviceFeatures.pNext = nullptr;
	void** lastPNext = &deviceFeatures.pNext;

	#if GARDEN_OS_MACOS
	vk::PhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	portabilityFeatures.mutableComparisonSamplers = VK_TRUE;
	*lastPNext = &portabilityFeatures;
	lastPNext = &portabilityFeatures.pNext;
	#endif

	if (hasPageableMemory)
	{
		pageableMemoryFeatures.pageableDeviceLocalMemory = true;
		pageableMemoryFeatures.pNext = nullptr;
		*lastPNext = &pageableMemoryFeatures;
		lastPNext = &pageableMemoryFeatures.pNext;
	}
	if (hasDynamicRendering)
	{
		dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
		dynamicRenderingFeatures.pNext = nullptr;
		*lastPNext = &dynamicRenderingFeatures;
		lastPNext = &dynamicRenderingFeatures.pNext;
	}
	if (hasDescriptorIndexing)
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

	vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, extensions, {}, &deviceFeatures);
	return physicalDevice.createDevice(deviceInfo);
}

//**********************************************************************************************************************
static void updateVkDynamicLoader(uint32 versionMajor, uint32 versionMinor,
	vk::Device device, vk::DispatchLoaderDynamic& dynamicLoader)
{
	PFN_vkVoidFunction pointer;

	if (versionMinor < 3)
	{
		pointer = vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR");
		if (pointer) dynamicLoader.vkCmdBeginRenderingKHR =
			PFN_vkCmdBeginRenderingKHR(pointer);
		pointer = vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR");
		if (pointer) dynamicLoader.vkCmdEndRenderingKHR =
			PFN_vkCmdEndRenderingKHR(pointer);
	}

	#if GARDEN_DEBUG
	pointer = vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
	if (pointer) dynamicLoader.vkSetDebugUtilsObjectNameEXT =
		PFN_vkSetDebugUtilsObjectNameEXT(pointer);
	pointer = vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT");
	if (pointer) dynamicLoader.vkCmdBeginDebugUtilsLabelEXT =
		PFN_vkCmdBeginDebugUtilsLabelEXT(pointer);
	pointer = vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT");
	if (pointer) dynamicLoader.vkCmdEndDebugUtilsLabelEXT =
		PFN_vkCmdEndDebugUtilsLabelEXT(pointer);
	#endif
}

//**********************************************************************************************************************
static VmaAllocator createVmaMemoryAllocator(uint32 majorVersion, uint32 minorVersion, vk::Instance instance,
	vk::PhysicalDevice physicalDevice, vk::Device device, bool hasMemoryBudget, bool hasMemoryPriority)
{
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.vulkanApiVersion = VK_MAKE_API_VERSION(0, majorVersion, minorVersion, 0);
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;

	if (hasMemoryBudget)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (hasMemoryPriority)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;

	VmaAllocator allocator = nullptr;
	auto result = vmaCreateAllocator(&allocatorInfo, &allocator);
	if (result != VK_SUCCESS)
		throw runtime_error("Failed to create memory allocator.");
	return allocator;
}

static vk::CommandPool createVkCommandPool(vk::Device device, uint32 queueFamilyIndex)
{
	vk::CommandPoolCreateInfo commandPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
	return device.createCommandPool(commandPoolInfo);
}

static vk::DescriptorPool createVkDescriptorPool(vk::Device device)
{
	// TODO: adjust based on a application usage

	const vector<vk::DescriptorPoolSize> sizes =
	{
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,
			VK_DS_POOL_COMBINED_SAMPLER_COUNT),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage,
			VK_DS_POOL_STORAGE_IMAGE_COUNT),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,
			VK_DS_POOL_UNIFORM_BUFFER_COUNT),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer,
			VK_DS_POOL_STORAGE_BUFFER_COUNT),
		vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment,
			VK_DS_POOL_INPUT_ATTACHMENT_COUNT),
	};
	
	uint32 maxSetCount = 0;
	for (uint32 i = 0; i < (uint32)sizes.size(); i++)
		maxSetCount += sizes[i].descriptorCount;

	vk::DescriptorPoolCreateInfo descriptorPoolInfo(
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		maxSetCount, (uint32)sizes.size(), sizes.data());
	return device.createDescriptorPool(descriptorPoolInfo);
}

//**********************************************************************************************************************
namespace
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
	const auto cacheHeaderSize = sizeof(PipelineCacheHeader) - sizeof(VkPipelineCacheHeaderVersionOne);
	auto path = Directory::getAppDataPath(appDataName) / "caches/shaders";
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
static void destroyPipelineCache(const string& appDataName, Version appVersion, vk::PipelineCache pipelineCache, 
	vk::Device device, const vk::PhysicalDeviceProperties2& deviceProperties)
{
	auto cacheData = Vulkan::device.getPipelineCacheData((VkPipelineCache)pipelineCache);
	if (cacheData.size() > sizeof(VkPipelineCacheHeaderVersionOne))
	{
		auto directory = Directory::getAppDataPath(appDataName) / "caches";
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
	device.destroyPipelineCache(pipelineCache);
}

//**********************************************************************************************************************
void Vulkan::initialize(const string& appName, const string& appDataName, Version appVersion,
	uint2 windowSize, bool isFullscreen, bool useVsync, bool useTripleBuffering, bool useThreading)
{
	GARDEN_ASSERT(!GraphicsAPI::isRunning);

	GraphicsAPI::appDataName = appDataName;
	GraphicsAPI::appVersion = appVersion;
	GraphicsAPI::isRunning = true;
	GraphicsAPI::graphicsPipelineVersion = 1;
	GraphicsAPI::computePipelineVersion = 1;
	GraphicsAPI::bufferVersion = 1;
	GraphicsAPI::imageVersion = 1;

	if (!glfwInit())
		throw runtime_error("Failed to initialize GLFW.");

	glfwSetErrorCallback([](int error_code, const char* description)
	{
		throw runtime_error("GLFW::ERROR: " + string(description) + "");
	});

	GLFWmonitor* primaryMonitor = nullptr;
	if (isFullscreen)
	{
		auto primaryMonitor = glfwGetPrimaryMonitor();
		auto videoMode = glfwGetVideoMode(primaryMonitor);
		glfwWindowHint(GLFW_REFRESH_RATE, videoMode->refreshRate);
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
		windowSize.x = videoMode->width;
		windowSize.y = videoMode->height;
	}
	
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	auto window = glfwCreateWindow(windowSize.x, windowSize.y,
		appName.c_str(), primaryMonitor, nullptr);
	if (!window)
		throw runtime_error("Failed to create GLFW window.");
	GraphicsAPI::window = window;

	#if GARDEN_OS_WINDOWS
	BOOL value = TRUE;
	auto hwnd = glfwGetWin32Window(window);
	::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
	#endif

	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	glfwSetWindowSizeLimits(window, GraphicsAPI::minFramebufferSize, 
		GraphicsAPI::minFramebufferSize, GLFW_DONT_CARE, GLFW_DONT_CARE);

	uint32 graphicsQueueMaxCount = 0, transferQueueMaxCount = 0, computeQueueMaxCount = 0;
	uint32 frameQueueIndex = 0, graphicsQueueIndex = 0, transferQueueIndex = 0, computeQueueIndex = 0;

	#if GARDEN_DEBUG
	instance = createVkInstance(appName, appVersion, versionMajor, versionMinor, hasDebugUtils);
	dynamicLoader = vk::DispatchLoaderDynamic(instance, vkGetInstanceProcAddr);
	if (hasDebugUtils)
		debugMessenger = createVkDebugMessenger(instance, dynamicLoader);
	#else
	instance = createVkInstance(appName, appVersion, versionMajor, versionMinor);
	dynamicLoader = vk::DispatchLoaderDynamic(instance, vkGetInstanceProcAddr);
	#endif
	
	physicalDevice = getBestPhysicalDevice(instance);
	deviceProperties = physicalDevice.getProperties2();
	versionMajor = VK_API_VERSION_MAJOR(deviceProperties.properties.apiVersion);
	versionMinor = VK_API_VERSION_MINOR(deviceProperties.properties.apiVersion);
	GraphicsAPI::isDeviceIntegrated = 
		deviceProperties.properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
	surface = createVkSurface(instance, window);
	getVkQueueFamilyIndices(physicalDevice, surface, graphicsQueueFamilyIndex,
		transferQueueFamilyIndex, computeQueueFamilyIndex, graphicsQueueMaxCount,
		transferQueueMaxCount, computeQueueMaxCount);
	deviceFeatures = physicalDevice.getFeatures2();
	device = createVkDevice(physicalDevice, versionMajor, versionMinor,
		graphicsQueueFamilyIndex, transferQueueFamilyIndex, computeQueueFamilyIndex,
		graphicsQueueMaxCount, transferQueueMaxCount, computeQueueMaxCount,
		frameQueueIndex, graphicsQueueIndex, transferQueueIndex, computeQueueIndex,
		hasMemoryBudget, hasMemoryPriority, hasPageableMemory,
		hasDynamicRendering, hasDescriptorIndexing);
	updateVkDynamicLoader(versionMajor, versionMinor, device, dynamicLoader);
	memoryAllocator = createVmaMemoryAllocator(versionMajor, versionMinor,
		instance, physicalDevice, device, hasMemoryBudget, hasMemoryPriority);
	frameQueue = device.getQueue(graphicsQueueFamilyIndex, frameQueueIndex);
	graphicsQueue = device.getQueue(graphicsQueueFamilyIndex, graphicsQueueIndex);
	transferQueue = device.getQueue(transferQueueFamilyIndex, transferQueueIndex);
	computeQueue = device.getQueue(computeQueueFamilyIndex, computeQueueIndex);
	frameCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	graphicsCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	transferCommandPool = createVkCommandPool(device, transferQueueFamilyIndex);
	computeCommandPool = createVkCommandPool(device, computeQueueFamilyIndex);
	descriptorPool = createVkDescriptorPool(device);
	pipelineCache = createPipelineCache(appDataName, appVersion, 
		device, deviceProperties, isCacheLoaded);

	int sizeX = 0, sizeY = 0;
	glfwGetFramebufferSize(window, &sizeX, &sizeY);
	swapchain = Swapchain(uint2(sizeX, sizeY), useVsync, useTripleBuffering, useThreading);

	GraphicsAPI::frameCommandBuffer.initialize(CommandBufferType::Frame);
	GraphicsAPI::graphicsCommandBuffer.initialize(CommandBufferType::Graphics);
	GraphicsAPI::transferCommandBuffer.initialize(CommandBufferType::TransferOnly);
	GraphicsAPI::computeCommandBuffer.initialize(CommandBufferType::ComputeOnly);
}

//**********************************************************************************************************************
void Vulkan::terminate()
{
	if (!GraphicsAPI::isRunning)
		return;

	// Should be set here, to destroy resources.
	GraphicsAPI::isRunning = false;

	GraphicsAPI::computeCommandBuffer.terminate();
	GraphicsAPI::transferCommandBuffer.terminate();
	GraphicsAPI::graphicsCommandBuffer.terminate();
	GraphicsAPI::frameCommandBuffer.terminate();

	for (int i = 0; i < frameLag + 1; i++)
		Vulkan::updateDestroyBuffer();
	swapchain.destroy();

	GraphicsAPI::descriptorSetPool.clear();
	GraphicsAPI::computePipelinePool.clear();
	GraphicsAPI::graphicsPipelinePool.clear();
	GraphicsAPI::framebufferPool.clear();
	GraphicsAPI::renderPasses.clear();
	GraphicsAPI::imageViewPool.clear();
	GraphicsAPI::imagePool.clear();
	GraphicsAPI::bufferPool.clear();

	if (device)
	{
		destroyPipelineCache(GraphicsAPI::appDataName, GraphicsAPI::appVersion, 
			pipelineCache, device, deviceProperties);
		device.destroyDescriptorPool(descriptorPool);
		device.destroyCommandPool(computeCommandPool);
		device.destroyCommandPool(transferCommandPool);
		device.destroyCommandPool(graphicsCommandPool);
		device.destroyCommandPool(frameCommandPool);
		vmaDestroyAllocator(memoryAllocator);
		device.destroy();
	}
	
	instance.destroySurfaceKHR(surface);
	glfwDestroyWindow((GLFWwindow*)GraphicsAPI::window);

	#if GARDEN_DEBUG
	if (hasDebugUtils)
		instance.destroy(debugMessenger, nullptr, dynamicLoader);
	#endif

	instance.destroy();
	glfwTerminate();
}

//**********************************************************************************************************************
void Vulkan::updateDestroyBuffer()
{
	auto& destroyBuffer = GraphicsAPI::destroyBuffers[GraphicsAPI::flushDestroyIndex];
	GraphicsAPI::flushDestroyIndex = (GraphicsAPI::flushDestroyIndex + 1) % (frameLag + 1);
	GraphicsAPI::fillDestroyIndex = (GraphicsAPI::fillDestroyIndex + 1) % (frameLag + 1);

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
				Vulkan::device.freeDescriptorSets(Vulkan::descriptorPool,
					resource.count, (vk::DescriptorSet*)resource.data0);
				free(resource.data0);
			}
			else
			{
				Vulkan::device.freeDescriptorSets(
					Vulkan::descriptorPool, 1, (vk::DescriptorSet*)&resource.data0);
			}
			break;
		case GraphicsAPI::DestroyResourceType::Pipeline:
			if (resource.count > 0)
			{
				for (uint32 i = 0; i < resource.count; i++)
					Vulkan::device.destroyPipeline(((VkPipeline*)resource.data0)[i]);
				free(resource.data0);
			}
			else
			{
				Vulkan::device.destroyPipeline((VkPipeline)resource.data0);
			}
			Vulkan::device.destroyPipelineLayout((VkPipelineLayout)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::DescriptorPool:
			Vulkan::device.destroyDescriptorPool((VkDescriptorPool)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::DescriptorSetLayout:
			Vulkan::device.destroyDescriptorSetLayout(
				(VkDescriptorSetLayout)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Sampler:
			Vulkan::device.destroySampler((VkSampler)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Framebuffer:
			Vulkan::device.destroyFramebuffer((VkFramebuffer)resource.data0);
			Vulkan::device.destroyRenderPass((VkRenderPass)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::ImageView:
			Vulkan::device.destroyImageView((VkImageView)resource.data0);
			break;
		case GraphicsAPI::DestroyResourceType::Image:
			vmaDestroyImage(Vulkan::memoryAllocator,
				(VkImage)resource.data0, (VmaAllocation)resource.data1);
			break;
		case GraphicsAPI::DestroyResourceType::Buffer:
			vmaDestroyBuffer(Vulkan::memoryAllocator,
				(VkBuffer)resource.data0, (VmaAllocation)resource.data1);
			break;
		default: abort();
		}
	}

	destroyBuffer.clear();
}