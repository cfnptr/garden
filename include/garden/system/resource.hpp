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
#include "math/aabb.hpp"
#include "garden/system/log.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/serialize.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

#include <memory>

#if !GARDEN_DEBUG
#include "pack/reader.hpp"
#endif

namespace garden
{

#define MODEL_VERTEX_SIZE (sizeof(float3) * 2 + sizeof(float2))
#define MODEL_POSITION_OFFSET 0
#define MODEL_NORMAL_OFFSET sizeof(float3)
#define MODEL_TEXCOORDS_OFFSET (sizeof(float3) * 2)

using namespace math;
using namespace ecsm;
using namespace garden::graphics;

class ResourceSystem;

//--------------------------------------------------------------------------------------------------
class Model final
{
public:
	class Node; class Mesh; class Primitive; class Attribute;
	class Accessor; class Material; class Texture;

	class Scene final
	{
	private:
		void* data = nullptr;
		Scene() = default;
		Scene(void* _data) : data(_data) { }
		friend class Model;
	public:
		string_view getName() const noexcept;
		uint32 getNodeCount() const noexcept;
		Node getNode(uint32 index) const noexcept;
	};
	class Node final
	{
		void* data = nullptr;
		Node() = default;
		Node(void* _data) : data(_data) { }
		friend class Scene;
	public:
		string_view getName() const noexcept;
		Node getParent() const noexcept;
		uint32 getChildrenCount() const noexcept;
		Node getChildren(uint32 index) const noexcept;
		float3 getPosition() const noexcept;
		float3 getScale() const noexcept;
		quat getRotation() const noexcept;
		bool hashMesh() const noexcept;
		Mesh getMesh() const noexcept;
		bool hasCamera() const noexcept;
		bool hasLight() const noexcept;
	};
	class Mesh final
	{
		void* data = nullptr;
		Mesh() = default;
		Mesh(void* _data) : data(_data) { }
		friend class Node;
	public:
		string_view getName() const noexcept;
		psize getPrimitiveCount() const noexcept;
		Primitive getPrimitive(psize index) const noexcept;
	};
	class Attribute final
	{
	public:
		enum class Type : uint8
		{
			Invalid, Position, Normal, Tangent, TexCoord,
			Color, Joints, Weights, Custom, Count
		};
	private:
		void* data = nullptr;
		Attribute() = default;
		Attribute(void* _data) : data(_data) { }
		friend class Primitive;
	public:
		Type getType() const noexcept;
		Accessor getAccessor() const noexcept;
	};
	class Primitive final
	{
	public:
		enum class Type : uint8
		{
			Points, Lines, LineLoop, LineStrip,
			Triangles, TriangleStrip, TriangleFan, Count
		};
	private:
		void* data = nullptr;
		Primitive() = default;
		Primitive(void* _data) : data(_data) { }
		friend class Mesh;
	public:
		Type getType() const noexcept;
		uint32 getAttributeCount() const noexcept;
		Attribute getAttribute(int32 index) const noexcept;
		Attribute getAttribute(Attribute::Type type) const noexcept;
		int32 getAttributeIndex(Attribute::Type type) const noexcept;
		Accessor getIndices() const noexcept;
		bool hasMaterial() const noexcept;
		Material getMaterial() const noexcept;

		psize getVertexCount(
			const vector<Attribute::Type>& attributes) const noexcept;
		static psize getBinaryStride(
			const vector<Attribute::Type>& attributes) noexcept;
		void copyVertices(const vector<Attribute::Type>& attributes,
			uint8* destination, psize count = 0, psize offset = 0) const noexcept;
	};
	class Accessor final
	{
	public:
		enum class ValueType : uint8
		{
			Invalid, Scalar, Vec2, Vec3, Vec4, Mat2, Mat3, Mat4, Count
		};
		enum class ComponentType : uint8
		{
			Invalid, R8, R8U, R16, R16U, R32U, R32F, Count
		};
	private:
		void* data = nullptr;
		Accessor() = default;
		Accessor(void* _data) : data(_data) { }
		friend class Primitive;
		friend class Attribute;
	public:
		ValueType getValueType() const noexcept;
		ComponentType getComponentType() const noexcept;
		Aabb getAabb() const noexcept;
		bool hasAabb() const noexcept;
		psize getCount() const noexcept;
		psize getStride() const noexcept;
		const uint8* getBuffer() const noexcept;
		psize getBinaryStride() const noexcept;

		void copy(uint8* destination, psize count = 0,
			psize offset = 0) const noexcept;
		void copy(uint8* destination, ComponentType componentType,
			psize count = 0, psize offset = 0) const noexcept;
	};
	class Material final
	{
	public:
		enum class AlphaMode : uint8
		{
			Opaque, Mask, Blend, Count
		};
	private:
		void* data = nullptr;
		Material() = default;
		Material(void* _data) : data(_data) { }
		friend class Primitive;
	public:
		string_view getName() const noexcept;
		bool isUnlit() const noexcept;
		bool isDoubleSided() const noexcept;
		AlphaMode getAlphaMode() const noexcept;
		float getAlphaCutoff() const noexcept;
		float3 getEmissiveFactor() const noexcept;
		bool hasBaseColorTexture() const noexcept;
		Texture getBaseColorTexture() const noexcept;
		float4 getBaseColorFactor() const noexcept;
		// Occlusion, Roughness, Metallic 
		bool hasOrmTexture() const noexcept;
		Texture getOrmTexture() const noexcept;
		float getMetallicFactor() const noexcept;
		float getRoughnessFactor() const noexcept;
		bool hasNormalTexture() const noexcept;
		Texture getNormalTexture() const noexcept;
	};
	class Texture final
	{
		void* data = nullptr;
		Texture() = default;
		Texture(void* _data) : data(_data) { }
		friend class Material;
	public:
		string_view getName() const noexcept;
		string_view getPath() const noexcept;
		const uint8* getBuffer() const noexcept;
		psize getBufferSize() const noexcept;
	};
private:
	fs::path relativePath;
	fs::path absolutePath;
	mutex buffersLocker;
	void* instance = nullptr;
	vector<uint8> data;
	bool isBuffersLoaded = false;	
	friend class ResourceSystem;
public:
	Model(void* _instance, fs::path&& _relativePath, fs::path&& _absolutePath) :
		relativePath(_relativePath), absolutePath(_absolutePath), instance(_instance) { }
	~Model();

	const fs::path& getRelativePath() const noexcept { return relativePath; }
	const fs::path& getAbsolutePath() const noexcept { return absolutePath; }

	uint32 getSceneCount() const noexcept;
	Scene getScene(uint32 index) const noexcept;
};

//--------------------------------------------------------------------------------------------------
class ResourceSystem : public System
{
public:
	enum class ImageFile : uint8
	{
		Webp, Exr, Png, Jpg, Hdr, Count
	};

	struct ModelVertex
	{
		float3 position = float3(0.0f);
		float3 normal = float3(0.0f);
		float2 texCoords = float2(0.0f);

		ModelVertex(const float3& position,
			const float3& normal, float2 texCoords) noexcept
		{
			this->position = position;
			this->normal = normal;
			this->texCoords = texCoords;
		}
		ModelVertex() = default;
	};
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
	ThreadSystem* threadSystem = nullptr;
	GraphicsSystem* graphicsSystem = nullptr;
	// TODO: We can use here lock free concurent queue.
	queue<GraphicsQueueItem> graphicsQueue;
	queue<ComputeQueueItem> computeQueue;
	queue<BufferQueueItem> bufferQueue;
	queue<ImageQueueItem> imageQueue;
	mutex queueLocker;

	static ResourceSystem* instance;
	ResourceSystem();

	// This system should be initialized before any resource loading.
	void initialize() override;
	void terminate() override;
	void update() override;
	
	friend class ecsm::Manager;
public:
//--------------------------------------------------------------------------------------------------
	static ResourceSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // no system
		return instance;
	}

	void loadImageData(const fs::path& path, vector<uint8>& data,
		int2& size, Image::Format& format, int32 taskIndex = -1) const;
	void loadCubemapData(const fs::path& path, vector<uint8>& left,
		vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top,
		vector<uint8>& back, vector<uint8>& front, int2& size,
		Image::Format& format, int32 taskIndex = -1) const;
	static void loadImageData(const uint8* data, psize dataSize, ImageFile fileType,
		vector<uint8>& pixels, int2& imageSize, Image::Format& format);

	Ref<Image> loadImage(const fs::path& path, Image::Bind bind,
		uint8 maxMipCount = 0, uint8 downscaleCount = 0,
		Image::Strategy strategy = Buffer::Strategy::Default,
		bool linearData = false, bool loadAsync = true);

	ID<GraphicsPipeline> loadGraphicsPipeline(const fs::path& path,
		ID<Framebuffer> framebuffer, bool useAsync = false, bool loadAsync = true,
		uint8 subpassIndex = 0, uint32 maxBindlessCount = 0,
		const map<string, GraphicsPipeline::SpecConst>& specConsts = {},
		const map<uint8, GraphicsPipeline::State>& stateOverrides = {});
	ID<ComputePipeline> loadComputePipeline(const fs::path& path,
		bool useAsync = false, bool loadAsync = true, uint32 maxBindlessCount = 0,
		const map<string, GraphicsPipeline::SpecConst>& specConsts = {});

	void loadScene(const fs::path& path);
	void storeScene(const fs::path& path);
	void clearScene();

	#if GARDEN_DEBUG
	shared_ptr<Model> loadModel(const fs::path& path);
	void loadModelBuffers(shared_ptr<Model> model);

	Ref<Buffer> loadBuffer(shared_ptr<Model> model, Model::Accessor accessor,
		Buffer::Bind bind, Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true); // TODO: offset, count?
	Ref<Buffer> loadVertexBuffer(shared_ptr<Model> model, Model::Primitive primitive,
		Buffer::Bind bind, const vector<Model::Attribute::Type>& attributes,
		Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true);
	#else
	pack::Reader& getPackReader() noexcept { return packReader; }
	#endif
};

//--------------------------------------------------------------------------------------------------
static psize toComponentCount(Model::Accessor::ValueType valueType)
{
	switch (valueType)
	{
		case Model::Accessor::ValueType::Scalar: return 1;
		case Model::Accessor::ValueType::Vec2: return 2;
		case Model::Accessor::ValueType::Vec3: return 3;
		case Model::Accessor::ValueType::Vec4: return 4;
		case Model::Accessor::ValueType::Mat2: return 4;
		case Model::Accessor::ValueType::Mat3: return 9;
		case Model::Accessor::ValueType::Mat4: return 16;
		default: abort();
	}
}
static psize toBinarySize(Model::Accessor::ComponentType componentType)
{
	switch (componentType)
	{
		case Model::Accessor::ComponentType::R8: return 1;
		case Model::Accessor::ComponentType::R8U: return 1;
		case Model::Accessor::ComponentType::R16: return 2;
		case Model::Accessor::ComponentType::R16U: return 2;
		case Model::Accessor::ComponentType::R32U: return 4;
		case Model::Accessor::ComponentType::R32F: return 4;
		default: abort();
	}
}
static psize toBinarySize(Model::Attribute::Type type)
{
	switch (type)
	{
		case Model::Attribute::Type::Position: return 12;
		case Model::Attribute::Type::Normal: return 12;
		case Model::Attribute::Type::Tangent: return 16;
		case Model::Attribute::Type::TexCoord: return 8;
		default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static ResourceSystem::ImageFile toImageFile(string_view name)
{
	if (name == "webp") return ResourceSystem::ImageFile::Webp;
	if (name == "exr") return ResourceSystem::ImageFile::Exr;
	if (name == "png") return ResourceSystem::ImageFile::Png;
	if (name == "jpg" || name == "jpeg") return ResourceSystem::ImageFile::Jpg;
	if (name == "hdr") return ResourceSystem::ImageFile::Hdr;
	throw runtime_error("Unknown image file type. (name: " + string(name) + ")");
}

} // garden