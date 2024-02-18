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
#include "math/matrix.hpp"
#include "math/aabb.hpp"

namespace garden
{

using namespace math;

#define MODEL_VERTEX_SIZE (sizeof(float3) * 2 + sizeof(float2))
#define MODEL_POSITION_OFFSET 0
#define MODEL_NORMAL_OFFSET sizeof(float3)
#define MODEL_TEXCOORDS_OFFSET (sizeof(float3) * 2)

/**
 * @brief 3D model data container.
 */
class ModelData final
{
public:
	class Node; class Mesh; class Primitive; class Attribute;
	class Accessor; class Material; class Texture;

	struct Vertex
	{
		float3 position = float3(0.0f);
		float3 normal = float3(0.0f);
		float2 texCoords = float2(0.0f);

		Vertex(const float3& position,
			const float3& normal, float2 texCoords) noexcept
		{
			this->position = position;
			this->normal = normal;
			this->texCoords = texCoords;
		}
		Vertex() = default;
	};

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
	ModelData(void* _instance, fs::path&& _relativePath, fs::path&& _absolutePath) :
		relativePath(_relativePath), absolutePath(_absolutePath), instance(_instance) { }
	~ModelData();

	const fs::path& getRelativePath() const noexcept { return relativePath; }
	const fs::path& getAbsolutePath() const noexcept { return absolutePath; }

	uint32 getSceneCount() const noexcept;
	Scene getScene(uint32 index) const noexcept;
};

//--------------------------------------------------------------------------------------------------
static psize toComponentCount(ModelData::Accessor::ValueType valueType)
{
	switch (valueType)
	{
		case ModelData::Accessor::ValueType::Scalar: return 1;
		case ModelData::Accessor::ValueType::Vec2: return 2;
		case ModelData::Accessor::ValueType::Vec3: return 3;
		case ModelData::Accessor::ValueType::Vec4: return 4;
		case ModelData::Accessor::ValueType::Mat2: return 4;
		case ModelData::Accessor::ValueType::Mat3: return 9;
		case ModelData::Accessor::ValueType::Mat4: return 16;
		default: abort();
	}
}
static psize toBinarySize(ModelData::Accessor::ComponentType componentType)
{
	switch (componentType)
	{
		case ModelData::Accessor::ComponentType::R8: return 1;
		case ModelData::Accessor::ComponentType::R8U: return 1;
		case ModelData::Accessor::ComponentType::R16: return 2;
		case ModelData::Accessor::ComponentType::R16U: return 2;
		case ModelData::Accessor::ComponentType::R32U: return 4;
		case ModelData::Accessor::ComponentType::R32F: return 4;
		default: abort();
	}
}
static psize toBinarySize(ModelData::Attribute::Type type)
{
	switch (type)
	{
		case ModelData::Attribute::Type::Position: return 12;
		case ModelData::Attribute::Type::Normal: return 12;
		case ModelData::Attribute::Type::Tangent: return 16;
		case ModelData::Attribute::Type::TexCoord: return 8;
		default: abort();
	}
}

}; // namespace garden