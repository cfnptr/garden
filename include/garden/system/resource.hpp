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
 */

#pragma once
#include "garden/resource/image.hpp"
#include "garden/system/graphics.hpp"
#include <queue>

#if !GARDEN_DEBUG
#include "pack/reader.hpp"
#endif

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace garden::graphics;

class ResourceSystem;

/***********************************************************************************************************************
 * @brief 
 */
class ResourceSystem : public System
{
protected:
	struct GraphicsQueueItem
	{
		void* renderPass = nullptr;
		GraphicsPipeline pipeline;
		ID<GraphicsPipeline> instance = {};
	};
	struct ComputeQueueItem
	{
		ComputePipeline pipeline;
		ID<ComputePipeline> instance = {};
	};
	struct BufferQueueItem
	{
		Buffer buffer;
		Buffer staging;
		ID<Buffer> instance = {};
	};
	struct ImageQueueItem
	{
		Image image;
		Buffer staging;
		ID<Image> instance = {};
	};

	#if !GARDEN_DEBUG
	pack::Reader packReader;
	#endif
	
	queue<GraphicsQueueItem> graphicsQueue; // TODO: We can use here lock free concurrent queue.
	queue<ComputeQueueItem> computeQueue;
	queue<BufferQueueItem> bufferQueue;
	queue<ImageQueueItem> imageQueue;
	mutex queueLocker;

	static ResourceSystem* instance;

	/**
	 * @brief Creates a new resource system instance.
	 * @param manager valid manager instance
	 */
	ResourceSystem(Manager* manager);
	/**
	 * @brief Destroys resource system instance.
	 */
	~ResourceSystem() final;

	void dequeuePipelines();
	void dequeueBuffers();

	void postDeinit();
	void input();
	
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief 
	 */
	void loadImageData(const fs::path& path, vector<uint8>& data,
		int2& size, Image::Format& format, int32 taskIndex = -1) const;

	/**
	 * @brief 
	 */
	void loadCubemapData(const fs::path& path, vector<uint8>& left,
		vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top,
		vector<uint8>& back, vector<uint8>& front, int2& size,
		Image::Format& format, int32 taskIndex = -1) const;
	
	/**
	 * @brief 
	 */
	static void loadImageData(const uint8* data, psize dataSize, ImageFileType fileType,
		vector<uint8>& pixels, int2& imageSize, Image::Format& format);

	/**
	 * @brief 
	 */
	Ref<Image> loadImage(const fs::path& path, Image::Bind bind,
		uint8 maxMipCount = 0, uint8 downscaleCount = 0,
		Image::Strategy strategy = Buffer::Strategy::Default,
		bool linearData = false, bool loadAsync = true);

	/**
	 * @brief 
	 */
	ID<GraphicsPipeline> loadGraphicsPipeline(const fs::path& path,
		ID<Framebuffer> framebuffer, bool useAsync = false, bool loadAsync = true,
		uint8 subpassIndex = 0, uint32 maxBindlessCount = 0,
		const map<string, GraphicsPipeline::SpecConst>& specConsts = {},
		const map<uint8, GraphicsPipeline::State>& stateOverrides = {});
	
	/**
	 * @brief 
	 */
	ID<ComputePipeline> loadComputePipeline(const fs::path& path,
		bool useAsync = false, bool loadAsync = true, uint32 maxBindlessCount = 0,
		const map<string, GraphicsPipeline::SpecConst>& specConsts = {});

	/**
	 * @brief 
	 */
	void loadScene(const fs::path& path);
	/**
	 * @brief 
	 */
	void storeScene(const fs::path& path);
	/**
	 * @brief 
	 */
	void clearScene();

	#if GARDEN_DEBUG
	/* TODO: refactor
	shared_ptr<Model> loadModel(const fs::path& path);
	void loadModelBuffers(shared_ptr<Model> model);

	Ref<Buffer> loadBuffer(shared_ptr<Model> model, ModelData::Accessor accessor,
		Buffer::Bind bind, Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true); // TODO: offset, count?
	Ref<Buffer> loadVertexBuffer(shared_ptr<Model> model, ModelData::Primitive primitive,
		Buffer::Bind bind, const vector<ModelData::Attribute::Type>& attributes,
		Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true);
	*/
	#else
	/**
	 * @brief Returns pack reader instance.
	 * @warning Use with caution, background task are using it at runtime.
	 */
	pack::Reader& getPackReader() noexcept { return packReader; }
	#endif

	/**
	 * @brief Returns resource system instance.
	 * @warning Do not use it if you have several logging system instances.
	 */
	static ResourceSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // Resource system is not created
		return instance;
	}
};

} // namespace garden