//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/graphics/vulkan.hpp"
#include "garden/graphics/primitive.hpp"
#include "garden/graphics/glfw.hpp"
#include "garden/xxhash.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <array>
#include <vector>
#include <thread>
#include <iostream>

using namespace std;
using namespace garden::graphics;
using namespace garden::graphics::primitive;

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
static const vk::DebugUtilsMessageSeverityFlagsEXT debugMessageSeverity =
	//vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
	vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
static const vk::DebugUtilsMessageTypeFlagsEXT debugMessageType =
	vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
	vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
	vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
	vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding;
#endif

//--------------------------------------------------------------------------------------------------
vk::Instance Vulkan::instance = {};
vk::DispatchLoaderDynamic Vulkan::dynamicLoader = {};
vk::PhysicalDevice Vulkan::physicalDevice = {};
vk::SurfaceKHR Vulkan::surface = {};
uint32 Vulkan::graphicsQueueFamilyIndex = 0;
uint32 Vulkan::transferQueueFamilyIndex = 0;
uint32 Vulkan::computeQueueFamilyIndex = 0;
vk::Device Vulkan::device = {};
VmaAllocator Vulkan::memoryAllocator = VK_NULL_HANDLE;
vk::Queue Vulkan::frameQueue = {};
vk::Queue Vulkan::graphicsQueue = {};
vk::Queue Vulkan::transferQueue = {};
vk::Queue Vulkan::computeQueue = {};
vk::CommandPool Vulkan::frameCommandPool = {};
vk::CommandPool Vulkan::graphicsCommandPool = {};
vk::CommandPool Vulkan::transferCommandPool = {};
vk::CommandPool Vulkan::computeCommandPool = {};
vk::DescriptorPool Vulkan::descriptorPool = {};
vector<vk::CommandBuffer> Vulkan::secondaryCommandBuffers;
vector<bool> Vulkan::secondaryCommandStates;
Swapchain Vulkan::swapchain;
vk::PhysicalDeviceProperties2 Vulkan::deviceProperties = {};
vk::PhysicalDeviceFeatures2 Vulkan::deviceFeatures = {};
bool Vulkan::hasMemoryBudget = false;
bool Vulkan::hasMemoryPriority = false;
bool Vulkan::hasPageableMemory = false;

#if GARDEN_DEBUG
vk::DebugUtilsMessengerEXT Vulkan::debugMessenger = {};
bool Vulkan::hasDebugUtils = false;
#endif

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
static VkBool32 VKAPI_CALL vkDebugMessengerCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData)
{
	const char* severity;
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
		severity = "VERBOSE";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		severity = "INFO";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		severity = "WARNING";
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		severity = "ERROR";
	else severity = "UNKNOWN";
	cout << "VULKAN::" << severity << ": " << callbackData->pMessage << "\n";

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		return VK_FALSE; // Breakpoint
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		return VK_FALSE; // Breakpoint
	return VK_FALSE;
}
#endif

//--------------------------------------------------------------------------------------------------
static bool hasExtension(const vector<const char*>& extensions, const char* extension)
{
	for (auto check : extensions)
	{
		if (strcmp(check, extension) == 0)
			return true;
	}

	return false;
}

//--------------------------------------------------------------------------------------------------
static vk::Instance createVkInstance(
	#if GARDEN_DEBUG
	bool& hasDebugUtils
	#endif
	)
{
	#if __APPLE__
	auto flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
	#else
	auto flags = vk::InstanceCreateFlags();
	#endif

	auto engineVersion = VK_MAKE_API_VERSION(0,
		GARDEN_VERSION_MAJOR, GARDEN_VERSION_MINOR, GARDEN_VERSION_PATCH);
	auto appVersion = VK_MAKE_API_VERSION(0,
		GARDEN_APP_VERSION_MAJOR, GARDEN_APP_VERSION_MINOR, GARDEN_APP_VERSION_PATCH);
	vk::ApplicationInfo appInfo(GARDEN_APP_NAME_STRING, appVersion,
		GARDEN_NAME_STRING, appVersion, VK_API_VERSION_1_3);
	
	vector<const char*> extensions;
	vector<const char*> layers;

	uint32 glfwExtensionCount = 0;
	auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	extensions.resize(glfwExtensionCount);

	for (uint32 i = 0; i < glfwExtensionCount; i++)
		extensions[i] = glfwExtensions[i];

	#if __APPLE__
	if (!hasExtension(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	#endif

	auto extensionProperties = vk::enumerateInstanceExtensionProperties();
	auto layerProperties = vk::enumerateInstanceLayerProperties();
	const void* instanceInfoNext = nullptr;

	#if GARDEN_DEBUG
	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo;
	for	(auto& properties : layerProperties)
	{
		if (strcmp(properties.layerName.data(), "VK_LAYER_KHRONOS_validation") == 0)
		{
			layers.push_back("VK_LAYER_KHRONOS_validation");
			break;
		}
	}
	hasDebugUtils = false;
	for	(auto& properties : extensionProperties)
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

	vk::InstanceCreateInfo instanceInfo(
		flags, &appInfo, layers, extensions, instanceInfoNext);
	return vk::createInstance(instanceInfo);
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
static vk::PhysicalDevice getBestPhysicalDevice(
	vk::Instance instance, bool& isDeviceIntegrated)
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

				isDeviceIntegrated = properties.deviceType ==
					vk::PhysicalDeviceType::eIntegratedGpu;
			}
		}
		
		return devices[targetIndex];
	}
	else
	{
		auto properties = devices[0].getProperties();
		isDeviceIntegrated = properties.deviceType ==
			vk::PhysicalDeviceType::eIntegratedGpu;
		return devices[0];
	}
}

//--------------------------------------------------------------------------------------------------
static vk::SurfaceKHR createVkSurface(vk::Instance instance, GLFWwindow* window)
{
	VkSurfaceKHR surface = nullptr;
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
		throw runtime_error("Failed to create window surface.");
	return vk::SurfaceKHR(surface);
}

//--------------------------------------------------------------------------------------------------
static void getVkQueueFamilyIndices(vk::PhysicalDevice physicalDevice,
	vk::SurfaceKHR surface, uint32& graphicsQueueFamilyIndex,
	uint32& transferQueueFamilyIndex, uint32& computeQueueFamilyIndex,
	uint32& graphicsQueueMaxCount, uint32& transferQueueMaxCount,
	uint32& computeQueueMaxCount)
{
	uint32 graphicsIndex = UINT32_MAX,
		transferIndex = UINT32_MAX, computeIndex = UINT32_MAX;
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
		if (properties[i].queueFlags & vk::QueueFlagBits::eTransfer &&
			graphicsIndex != i)
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
			if (properties[i].queueFlags & vk::QueueFlagBits::eCompute &&
				graphicsIndex != i)
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

//--------------------------------------------------------------------------------------------------
static vk::Device createVkDevice(vk::PhysicalDevice physicalDevice,
	uint32 graphicsQueueFamilyIndex, uint32 transferQueueFamilyIndex,
	uint32 computeQueueFamilyIndex, uint32 graphicsQueueMaxCount,
	uint32 transferQueueMaxCount, uint32 computeQueueMaxCount,
	uint32& frameQueueIndex, uint32& graphicsQueueIndex,
	uint32& transferQueueIndex, uint32& computeQueueIndex,
	bool& hasMemoryBudget, bool& hasMemoryPriority, bool& hasPageableMemory)
{
	uint32 graphicsQueueCount = 1,
		transferQueueCount = 0, computeQueueCount = 0;
	frameQueueIndex = 0;

	if (graphicsQueueCount < graphicsQueueMaxCount)
		graphicsQueueIndex = graphicsQueueCount++;

	if (transferQueueFamilyIndex == graphicsQueueFamilyIndex)
	{
		if (graphicsQueueCount < graphicsQueueMaxCount)
			transferQueueIndex = graphicsQueueCount++;
		else transferQueueIndex = graphicsQueueIndex;
	}
	else if (transferQueueFamilyIndex == computeQueueFamilyIndex)
	{
		if (computeQueueCount < computeQueueMaxCount)
			transferQueueIndex = computeQueueCount++;
		else transferQueueIndex = computeQueueIndex;
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
		else computeQueueIndex = graphicsQueueIndex;
	}
	else if (computeQueueFamilyIndex == transferQueueFamilyIndex)
	{
		if (transferQueueCount < transferQueueMaxCount)
			computeQueueIndex = transferQueueCount++;
		else computeQueueIndex = transferQueueCount;
	}
	else
	{
		computeQueueCount = 1;
		computeQueueIndex = 0;
	}

	vector<float> queuePriorities(std::max(std::max(
		graphicsQueueCount, transferQueueCount), computeQueueCount), 1.0f);

	vector<vk::DeviceQueueCreateInfo> queueInfos =
	{
		vk::DeviceQueueCreateInfo({}, graphicsQueueFamilyIndex,
			graphicsQueueCount, queuePriorities.data())
	};

	if (transferQueueCount > 0)
	{
		queueInfos.push_back(vk::DeviceQueueCreateInfo({},
		transferQueueFamilyIndex, transferQueueCount, queuePriorities.data()));
	}
	if (computeQueueCount > 0)
	{
		queueInfos.push_back(vk::DeviceQueueCreateInfo({},
			computeQueueFamilyIndex, computeQueueCount, queuePriorities.data()));
	}
	
	vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		#if __APPLE__
		VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
		#endif
	};

	auto extensionProperties = physicalDevice.enumerateDeviceExtensionProperties();
	for (auto& properties : extensionProperties)
	{
		if (strcmp(properties.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
			hasMemoryBudget = true;
		if (strcmp(properties.extensionName, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) == 0)
			hasMemoryPriority = true;
		if (strcmp(properties.extensionName, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME) == 0)
			hasPageableMemory = true;
	}

	if (hasMemoryBudget && !hasExtension(extensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
		extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (hasMemoryPriority && !hasExtension(extensions, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME))
		extensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
	if (hasPageableMemory && !hasExtension(extensions, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME))
		extensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);

	vk::PhysicalDeviceMaintenance4Features maintenance4Features;
	maintenance4Features.maintenance4 = VK_TRUE;
	void** lastPNext = &maintenance4Features.pNext;

	vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
	dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
	dynamicRenderingFeatures.pNext = &maintenance4Features;

	vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures;
	descriptorIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
	descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
	descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
	descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
	descriptorIndexingFeatures.pNext = &dynamicRenderingFeatures;

	vk::PhysicalDeviceFeatures2 deviceFeatures;
	deviceFeatures.features.independentBlend = VK_TRUE;
	deviceFeatures.features.depthClamp = VK_TRUE;
	deviceFeatures.pNext = &descriptorIndexingFeatures;

	#if __APPLE__
	vk::PhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	portabilityFeatures.mutableComparisonSamplers = VK_TRUE;
	*lastPNext = &portabilityFeatures;
	lastPNext = &portabilityFeatures.pNext;
	#endif

	vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableMemoryFeatures;
	if (hasPageableMemory)
	{
		pageableMemoryFeatures.pageableDeviceLocalMemory = true;
		*lastPNext = &pageableMemoryFeatures;
		lastPNext = &pageableMemoryFeatures.pNext;
	}

	vk::DeviceCreateInfo deviceInfo({}, queueInfos, {}, extensions, {}, &deviceFeatures);
	return physicalDevice.createDevice(deviceInfo);
}

//--------------------------------------------------------------------------------------------------
static void updateVkDynamicLoader(vk::Device device,
	vk::DispatchLoaderDynamic& dynamicLoader)
{
	auto pointer = vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
	if (pointer) dynamicLoader.vkSetDebugUtilsObjectNameEXT =
		PFN_vkSetDebugUtilsObjectNameEXT(pointer);
	pointer = vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT");
	if (pointer) dynamicLoader.vkCmdBeginDebugUtilsLabelEXT =
		PFN_vkCmdBeginDebugUtilsLabelEXT(pointer);
	pointer = vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT");
	if (pointer) dynamicLoader.vkCmdEndDebugUtilsLabelEXT =
		PFN_vkCmdEndDebugUtilsLabelEXT(pointer);
}

//--------------------------------------------------------------------------------------------------
static VmaAllocator createVmaMemoryAllocator(vk::Instance instance,
	vk::PhysicalDevice physicalDevice, vk::Device device,
	bool hasMemoryBudget, bool hasMemoryPriority)
{
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	if (hasMemoryBudget) allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (hasMemoryPriority) allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
	VmaAllocator allocator = VK_NULL_HANDLE;
	auto result = vmaCreateAllocator(&allocatorInfo, &allocator);
	if (result != VK_SUCCESS) throw runtime_error("Failed to create memory allocator.");
	return allocator;
}

//--------------------------------------------------------------------------------------------------
static vk::CommandPool createVkCommandPool(vk::Device device,
	uint32 queueFamilyIndex, bool isTransient = false)
{
	vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	if (isTransient) flags |= vk::CommandPoolCreateFlagBits::eTransient;
	vk::CommandPoolCreateInfo commandPoolInfo(flags, queueFamilyIndex);
	return device.createCommandPool(commandPoolInfo);
}

//--------------------------------------------------------------------------------------------------
static vk::DescriptorPool createVkDescriptorPool(vk::Device device)
{
	// TODO: adjust based on a game usage

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

//--------------------------------------------------------------------------------------------------
void Vulkan::initialize(int2 windowSize, bool isFullscreen,
	bool useVsync, bool useTripleBuffering, bool useThreading)
{
	GARDEN_ASSERT(!GraphicsAPI::isRunning);

	GraphicsAPI::isRunning = true;
	GraphicsAPI::hashState = XXH3_createState();
	GraphicsAPI::graphicsPipelineVersion = 1;
	GraphicsAPI::computePipelineVersion = 1;
	GraphicsAPI::bufferVersion = 1;
	GraphicsAPI::imageVersion = 1;

	if (!glfwInit()) throw runtime_error("Failed to initialize GLFW.");

	glfwSetErrorCallback([](int error_code, const char* description) {
		throw runtime_error("GLFW::ERROR: " + string(description) + ""); });

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
		GARDEN_APP_NAME_STRING, primaryMonitor, NULL);
	if (!window) throw runtime_error("Failed to create GLFW window.");
	GraphicsAPI::window = window;

	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	glfwSetWindowSizeLimits(window, MIN_DISPLAY_SIZE, MIN_DISPLAY_SIZE,
		GLFW_DONT_CARE, GLFW_DONT_CARE);

	/* TODO: use if required
	#if __APPLE__
	auto libMoltenVK = dlopen("libMoltenVK.dylib", RTLD_LAZY);
	auto getMoltenVKConfigurationMVK = (PFN_vkGetMoltenVKConfigurationMVK)
		dlsym(libMoltenVK, "vkGetMoltenVKConfigurationMVK");
	auto setMoltenVKConfigurationMVK = (PFN_vkSetMoltenVKConfigurationMVK)
		dlsym(libMoltenVK, "vkSetMoltenVKConfigurationMVK");
	if (!getMoltenVKConfigurationMVK || !setMoltenVKConfigurationMVK)
		throw runtime_error("Failed to get MoltenVK configuration functions.");

	MVKConfiguration mvkConfiguration;
	psize mvkConfigurationSize = sizeof(MVKConfiguration);

	auto vkResult = getMoltenVKConfigurationMVK(VK_NULL_HANDLE,
		&mvkConfiguration, &mvkConfigurationSize);
	if (vkResult != VK_SUCCESS)
		throw runtime_error("Failed to get MoltenVK configuration.");

	// configure

	vkResult = setMoltenVKConfigurationMVK(VK_NULL_HANDLE, 
		&mvkConfiguration, &mvkConfigurationSize);
	if (vkResult != VK_SUCCESS)
		throw runtime_error("Failed to set MoltenVK configuration.");
	#endif
	*/

	#if __APPLE__ // TODO: MoltenVK should use by default when Metal 3 arrives.
	// setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
	// Unfortunatly xcode graphics debugger is not working with this flag :(
	#endif

	uint32 graphicsQueueMaxCount = 0,
		transferQueueMaxCount = 0, computeQueueMaxCount = 0;
	uint32 frameQueueIndex = 0, graphicsQueueIndex = 0,
		transferQueueIndex = 0, computeQueueIndex = 0;

	#if GARDEN_DEBUG
	instance = createVkInstance(hasDebugUtils);
	dynamicLoader = vk::DispatchLoaderDynamic(instance, vkGetInstanceProcAddr);
	if (hasDebugUtils) debugMessenger = createVkDebugMessenger(instance, dynamicLoader);
	#else
	instance = createVkInstance();
	dynamicLoader = vk::DispatchLoaderDynamic(instance, vkGetInstanceProcAddr);
	#endif
	
	physicalDevice = getBestPhysicalDevice(instance, GraphicsAPI::isDeviceIntegrated);
	surface = createVkSurface(instance, window);
	getVkQueueFamilyIndices(physicalDevice, surface, graphicsQueueFamilyIndex,
		transferQueueFamilyIndex, computeQueueFamilyIndex, graphicsQueueMaxCount,
		transferQueueMaxCount, computeQueueMaxCount);
	deviceProperties = physicalDevice.getProperties2();
	deviceFeatures = physicalDevice.getFeatures2();
	device = createVkDevice(physicalDevice, graphicsQueueFamilyIndex,
		transferQueueFamilyIndex, computeQueueFamilyIndex,
		graphicsQueueMaxCount, transferQueueMaxCount, computeQueueMaxCount,
		frameQueueIndex, graphicsQueueIndex, transferQueueIndex, computeQueueIndex,
		hasMemoryBudget, hasMemoryPriority, hasPageableMemory);
	updateVkDynamicLoader(device, dynamicLoader);
	memoryAllocator = createVmaMemoryAllocator(instance,
		physicalDevice, device, hasMemoryBudget, hasMemoryPriority);
	frameQueue = device.getQueue(graphicsQueueFamilyIndex, frameQueueIndex);
	graphicsQueue = device.getQueue(graphicsQueueFamilyIndex, graphicsQueueIndex);
	transferQueue = device.getQueue(transferQueueFamilyIndex, transferQueueIndex);
	computeQueue = device.getQueue(computeQueueFamilyIndex, computeQueueIndex);
	frameCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	graphicsCommandPool = createVkCommandPool(device, graphicsQueueFamilyIndex);
	transferCommandPool = createVkCommandPool(device, transferQueueFamilyIndex);
	computeCommandPool = createVkCommandPool(device, computeQueueFamilyIndex); 
	descriptorPool = createVkDescriptorPool(device);
	GraphicsAPI::frameCommandBuffer.initialize(CommandBufferType::Frame);
	GraphicsAPI::graphicsCommandBuffer.initialize(CommandBufferType::Graphics);
	GraphicsAPI::transferCommandBuffer.initialize(CommandBufferType::TransferOnly);
	GraphicsAPI::computeCommandBuffer.initialize(CommandBufferType::ComputeOnly);

	int2 framebufferSize;
	glfwGetFramebufferSize(window, &framebufferSize.x, &framebufferSize.y);
	swapchain = Swapchain(framebufferSize, useVsync, useTripleBuffering, useThreading);
}

//--------------------------------------------------------------------------------------------------
void Vulkan::terminate()
{
	if (!GraphicsAPI::isRunning) return;

	// Should be set here, to destroy resources.
	GraphicsAPI::isRunning = false;
	Vulkan::updateDestroyBuffer();
	swapchain.destroy();

	GraphicsAPI::computeCommandBuffer.terminate();
	GraphicsAPI::transferCommandBuffer.terminate();
	GraphicsAPI::graphicsCommandBuffer.terminate();
	GraphicsAPI::frameCommandBuffer.terminate();

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
	if (hasDebugUtils) instance.destroy(debugMessenger, nullptr, dynamicLoader);
	#endif

	instance.destroy();
	glfwTerminate();
	XXH3_freeState((XXH3_state_t*)GraphicsAPI::hashState);
}

//--------------------------------------------------------------------------------------------------
void Vulkan::updateDestroyBuffer()
{
	if (GraphicsAPI::destroyBuffer.empty()) return;

	sort(GraphicsAPI::destroyBuffer.begin(), GraphicsAPI::destroyBuffer.end(),
		[](const GraphicsAPI::DestroyResource& a, const GraphicsAPI::DestroyResource& b)
		{ return (uint32)a.type < (uint32)b.type; });

	for (auto& resource : GraphicsAPI::destroyBuffer)
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
			Vulkan::device.destroyPipelineCache((VkPipelineCache)resource.data2);
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

	GraphicsAPI::destroyBuffer.clear();
}