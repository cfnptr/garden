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

#pragma once
#include "garden/system/log.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/transform.hpp"
#include "garden/graphics/constants.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

namespace garden
{

#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720

using namespace ecsm;
using namespace garden::graphics;
class GraphicsSystem;

//--------------------------------------------------------------------------------------------------
enum class KeyboardButton : int
{
	Unknown = -1,
	Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
	N0 = 48, N1 = 49, N2 = 50, N3 = 51, N4 = 52,
	N5 = 53, N6 = 54, N7 = 55, N8 = 56, N9 = 57,
	Semicolon = 59, Equal = 61,
	A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73, J = 74,
	K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82, S = 83, T = 84,
	U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
	LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
	World1 = 161, World2 = 162,
	Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
	Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267,
	Home = 268, End = 269,
	CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283, Pause = 284,
	F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295, F7 = 296, F8 = 297,
	F9 = 298, F10 = 299, F11 = 300, F12 = 301, F13 = 302, F14 = 303, F15 = 304,
	F16 = 305, F17 = 306, F18 = 307, F19 = 308, F20 = 309, F21 = 310, F22 = 311,
	F23 = 312, F24 = 313, F25 = 314,
	KP_0 = 320, KP_1 = 321, KP_2 = 322, KP_3 = 323, KP_4 = 324, KP_5 = 325, KP_6 = 326,
	KP_7 = 327, KP_8 = 328, KP_9 = 329, KP_Decimal = 330, KP_Divide = 331,
	KP_Multiply = 332, KP_Subtract = 333, KP_Add = 334, KP_Enter = 335, KP_Equal = 336,
	LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
	RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347, Menu = 348,
	Last = Menu
};
enum class MouseButton : int
{
	N1 = 0, N2 = 1, N3 = 2, N4 = 3, N5 = 4, N6 = 5, N7 = 6, N8 = 7,
	Last = N8, Left = N1, Right = N2, Middle = N3
};

//--------------------------------------------------------------------------------------------------
enum class CursorMode : int
{
	Default = 0x00034001, Hidden = 0x00034002, Locked = 0x00034003,
};
enum class CursorType : uint8
{
	Default = 0, Ibeam = 1, Crosshair = 2, Hand = 3, Hresize = 4, Vresize = 5, Count = 6,
};

//--------------------------------------------------------------------------------------------------
class IRenderSystem
{
public:
	struct SwapchainChanges final
	{
		bool framebufferSize = false;
		bool bufferCount = false;
		bool vsyncState = false;
	};
private:
	GraphicsSystem* graphicsSystem = nullptr;
	friend class GraphicsSystem;
protected:
	virtual void render() { }
	virtual void recreateSwapchain(const SwapchainChanges& changes) { }
public:
	GraphicsSystem* getGraphicsSystem() noexcept
	{
		GARDEN_ASSERT(graphicsSystem);
		return graphicsSystem;
	}
	const GraphicsSystem* getGraphicsSystem() const noexcept
	{
		GARDEN_ASSERT(graphicsSystem);
		return graphicsSystem;
	}
};

//--------------------------------------------------------------------------------------------------
class GraphicsSystem final : public System
{
	LogSystem* logSystem = nullptr;
	vector<vector<ID<Buffer>>> cameraConstantsBuffers;
	vector<function<void(const char**, uint32)>> onFileDrops;
	int2 framebufferSize = int2(0), windowSize = int2(0);
	float2 cursorPosition = float2(0.0f);
	double time = 0.0, deltaTime = 0.0;
	uint64 frameIndex = 0;
	CursorMode cursorMode = CursorMode::Default;
	ID<Framebuffer> swapchainFramebuffer = {};
	ID<Buffer> fullCubeVertices = {};
	ID<Image> whiteTexture = {};
	ID<Image> greenTexture = {};
	ID<Image> normalMapTexture = {};
	CameraConstants currentCameraConstants = {};
	bool useThreading = false;
	bool forceRecreateSwapchain = false;
	bool isF11Pressed = false;
	IRenderSystem::SwapchainChanges swapchainChanges;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	ID<GraphicsPipeline> aabbPipeline;
	#endif

	// TODO: support offscreen rendering mode.

	GraphicsSystem(int2 windowSize = int2(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT),
		bool isFullscreen = !GARDEN_DEBUG, bool isBorderless = false, bool useVsync = true,
		bool useTripleBuffering = true, bool useThreading = false);
	~GraphicsSystem() final;

	#if GARDEN_EDITOR
	void initializeImGui();
	void terminateImGui();
	void recreateImGui();
	#endif

	void initialize() final;
	void terminate() final;
	void update() final;
	void disposeComponents() final;

	static void onFileDrop(void* window, int count, const char** paths);
	friend class ecsm::Manager;
public:
	ID<Entity> camera = {};
	ID<Entity> directionalLight = {};
	bool useVsync = false, useTripleBuffering = false;

//--------------------------------------------------------------------------------------------------
	double getTime() const noexcept { return time; }
	double getDeltaTime() const noexcept { return deltaTime; }
	uint64 getFrameIndex() const noexcept { return frameIndex; }
	int2 getFramebufferSize() const noexcept { return framebufferSize; }
	int2 getWindowSize() const noexcept { return windowSize; }
	float2 getCursorPosition() const noexcept { return cursorPosition; }
	uint32 getSwapchainSize() const;
	uint32 getSwapchainIndex() const;
	bool isKeyboardButtonPressed(KeyboardButton button) const;
	bool isMouseButtonPressed(MouseButton button) const;

	CursorMode getCursorMode() const noexcept { return cursorMode; }
	void setCursorMode(CursorMode mode);

	ID<Buffer> getFullCubeVertices();
	ID<Image> getWhiteTexture();
	ID<Image> getGreenTexture();
	ID<Image> getNormalMapTexture();

	ID<Framebuffer> getSwapchainFramebuffer()
		const noexcept { return swapchainFramebuffer; }
	const vector<vector<ID<Buffer>>>& getCameraConstantsBuffers()
		const noexcept { return cameraConstantsBuffers; }
	void recreateSwapchain(const IRenderSystem::SwapchainChanges& changes);

	void registerFileDrop(function<void(const char**, uint32)> onFileDrop)
		{ onFileDrops.push_back(onFileDrop); }

	#if GARDEN_DEBUG || GARDEN_EDITOR
//--------------------------------------------------------------------------------------------------
	void setDebugName(ID<Buffer> instance, const string& name);
	void setDebugName(ID<Image> instance, const string& name);
	void setDebugName(ID<ImageView> instance, const string& name);
	void setDebugName(ID<Framebuffer> instance, const string& name);
	void setDebugName(ID<DescriptorSet> instance, const string& name);
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name) \
		graphicsSystem->setDebugName(resource, name)
	#else
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name)
	#endif

//--------------------------------------------------------------------------------------------------
// Returns current render call data.
//--------------------------------------------------------------------------------------------------

	const CameraConstants& getCurrentCameraConstants()
		const noexcept { return currentCameraConstants; }

//--------------------------------------------------------------------------------------------------
	ID<Buffer> createBuffer(Buffer::Bind bind,
		Buffer::Usage usage, const void* data, uint64 size);
	
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Usage usage, uint64 size)
	{
		return createBuffer(bind, usage, nullptr, size);
	}
	template<typename T = float>
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Usage usage,
		const vector<T>& data, psize count = 0, psize offset = 0)
	{
		if (count == 0)
		{
			return createBuffer(bind, usage, data.data() + offset,
				(data.size() - offset) * sizeof(T));
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, usage, data.data() + offset, count * sizeof(T));
		}
	}
	template<typename T = float, psize S>
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Usage usage,
		const array<T, S>& data, psize count = 0, psize offset = 0)
	{
		if (count == 0)
		{
			return createBuffer(bind, usage, data.data() + offset,
				(data.size() - offset) * sizeof(T));
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, usage, data.data() + offset, count * sizeof(T));
		}
	}

	void destroy(ID<Buffer> instance);
	View<Buffer> get(ID<Buffer> instance) const;

//--------------------------------------------------------------------------------------------------
	ID<Image> createImage(Image::Type type, Image::Format format,
		Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Format dataFormat = Image::Format::Undefined);
	
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		return createImage(Image::Type::Texture3D, format, bind, data, size, dataFormat);
	}
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, int2 size,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ?
			Image::Type::Texture2DArray : Image::Type::Texture2D;
		return createImage(imageType, format, bind, data, int3(size, 1), dataFormat);
	}
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, int32 size,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ?
			Image::Type::Texture1DArray : Image::Type::Texture1D;
		return createImage(imageType, format, bind, data, int3(size, 1, 1), dataFormat);
	}

	/* TODO: create 2 images with the same shared memory allocation.
	// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
	ID<Image> createSharedImage(Image::Type type0, Image::Format format0,
		Image::Bind bind0, const Image::Mips& data0, const int3& size0,
		Image::Format dataFormat0, Image::Type type1, Image::Format format1,
		Image::Bind bind1, const Image::Mips& data1, const int3& size1,
		Image::Format dataFormat);
	*/
	
	void destroy(ID<Image> instance);
	View<Image> get(ID<Image> instance) const;

//--------------------------------------------------------------------------------------------------
	ID<ImageView> createImageView(ID<Image> image, Image::Type type,
		Image::Format format = Image::Format::Undefined, uint8 baseMip = 0,
		uint8 mipCount = 1, uint32 baseLayer = 0, uint32 layerCount = 1);
	void destroy(ID<ImageView> instance);
	View<ImageView> get(ID<ImageView> instance) const;

//--------------------------------------------------------------------------------------------------
	ID<Framebuffer> createFramebuffer(int2 size,
		vector<Framebuffer::OutputAttachment>&& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment = {});
	ID<Framebuffer> createFramebuffer(int2 size,
		vector<Framebuffer::Subpass>&& subpasses);
	void destroy(ID<Framebuffer> instance);
	View<Framebuffer> get(ID<Framebuffer> instance) const;

//--------------------------------------------------------------------------------------------------
	void destroy(ID<GraphicsPipeline> instance);
	View<GraphicsPipeline> get(ID<GraphicsPipeline> instance) const;

	void destroy(ID<ComputePipeline> instance);
	View<ComputePipeline> get(ID<ComputePipeline> instance) const;

//--------------------------------------------------------------------------------------------------
	ID<DescriptorSet> createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);
	ID<DescriptorSet> createDescriptorSet(ID<ComputePipeline> computePipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);
	void destroy(ID<DescriptorSet> instance);
	View<DescriptorSet> get(ID<DescriptorSet> instance) const;
	
//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	bool isRecording() const noexcept;
	void startRecording(CommandBufferType commandBufferType = CommandBufferType::Frame);
	void stopRecording();

	#if GARDEN_DEBUG || GARDEN_EDITOR
	void drawAabb(const float4x4& mvp, const float4& color = float4(1.0f));
	#endif
};

} // garden