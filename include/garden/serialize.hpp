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

/***********************************************************************************************************************
 * @file
 * @brief Data serialization functions.
 * 
 * @details
 * Data serialization is the process of converting structured data, such as objects or data structures, 
 * into a format that can be easily stored, transmitted, or reconstructed later. This serialized data 
 * can then be saved to a file, sent over a network, or otherwise persisted.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"

#include "math/aabb.hpp"
#include "math/color.hpp"
#include "math/quaternion.hpp"
#include <random>

namespace garden
{

using namespace ecsm;

/**
 * @brief Base serializer interface.
 */
class ISerializer
{
public:
	random_device randomDevice;

	virtual ~ISerializer() { }

	virtual void beginChild(string_view name) = 0;
	virtual void endChild() = 0;

	virtual void beginArrayElement() = 0;
	virtual void write(int64 value) = 0;
	virtual void write(uint64 value) = 0;
	virtual void write(int32 value) = 0;
	virtual void write(uint32 value) = 0;
	virtual void write(int16 value) = 0;
	virtual void write(uint16 value) = 0;
	virtual void write(int8 value) = 0;
	virtual void write(uint8 value) = 0;
	virtual void write(bool value) = 0;
	virtual void write(float value) = 0;
	virtual void write(double value) = 0;
	virtual void write(string_view value) = 0;
	virtual void endArrayElement() = 0;

	virtual void write(string_view name, int64 value) = 0;
	virtual void write(string_view name, uint64 value) = 0;
	virtual void write(string_view name, int32 value) = 0;
	virtual void write(string_view name, uint32 value) = 0;
	virtual void write(string_view name, int16 value) = 0;
	virtual void write(string_view name, uint16 value) = 0;
	virtual void write(string_view name, int8 value) = 0;
	virtual void write(string_view name, uint8 value) = 0;
	virtual void write(string_view name, bool value) = 0;
	virtual void write(string_view name, float value) = 0;
	virtual void write(string_view name, double value) = 0;
	virtual void write(string_view name, string_view value) = 0;
	virtual void write(string_view name, u32string_view value) = 0;
	virtual void write(string_view name, int2 value) = 0;
	virtual void write(string_view name, int3 value) = 0;
	virtual void write(string_view name, int4 value) = 0;
	virtual void write(string_view name, uint2 value) = 0;
	virtual void write(string_view name, uint3 value) = 0;
	virtual void write(string_view name, uint4 value) = 0;
	virtual void write(string_view name, float2 value) = 0;
	virtual void write(string_view name, float3 value) = 0;
	virtual void write(string_view name, float4 value) = 0;
	virtual void write(string_view name, quat value) = 0;
	virtual void write(string_view name, const float2x2& value) = 0;
	virtual void write(string_view name, const float3x3& value) = 0;
	virtual void write(string_view name, const float4x4& value) = 0;
	virtual void write(string_view name, const Aabb& value) = 0;
	virtual void write(string_view name, Color value, bool rgb = false) = 0;
	// TODO: write array of values.
};

/**
 * @brief Base deserializer interface.
 */
class IDeserializer
{
public:
	virtual ~IDeserializer() { }

	virtual bool beginChild(string_view name) = 0;
	virtual void endChild() = 0;

	virtual psize getArraySize() = 0;
	virtual bool beginArrayElement(psize index) = 0;
	virtual bool read(int64& value) = 0;
	virtual bool read(uint64& value) = 0;
	virtual bool read(int32& value) = 0;
	virtual bool read(uint32& value) = 0;
	virtual bool read(int16& value) = 0;
	virtual bool read(uint16& value) = 0;
	virtual bool read(int8& value) = 0;
	virtual bool read(uint8& value) = 0;
	virtual bool read(bool& value) = 0;
	virtual bool read(float& value) = 0;
	virtual bool read(double& value) = 0;
	virtual bool read(string& value) = 0;
	virtual void endArrayElement() = 0;

	virtual bool read(string_view name, int64& value) = 0;
	virtual bool read(string_view name, uint64& value) = 0;
	virtual bool read(string_view name, int32& value) = 0;
	virtual bool read(string_view name, uint32& value) = 0;
	virtual bool read(string_view name, int16& value) = 0;
	virtual bool read(string_view name, uint16& value) = 0;
	virtual bool read(string_view name, int8& value) = 0;
	virtual bool read(string_view name, uint8& value) = 0;
	virtual bool read(string_view name, bool& value) = 0;
	virtual bool read(string_view name, float& value) = 0;
	virtual bool read(string_view name, double& value) = 0;
	virtual bool read(string_view name, string& value) = 0;
	virtual bool read(string_view name, u32string& value) = 0;
	virtual bool read(string_view name, int2& value) = 0;
	virtual bool read(string_view name, int3& value) = 0;
	virtual bool read(string_view name, int4& value) = 0;
	virtual bool read(string_view name, uint2& value) = 0;
	virtual bool read(string_view name, uint3& value) = 0;
	virtual bool read(string_view name, uint4& value) = 0;
	virtual bool read(string_view name, float2& value) = 0;
	virtual bool read(string_view name, float3& value) = 0;
	virtual bool read(string_view name, float4& value) = 0;
	virtual bool read(string_view name, quat& value) = 0;
	virtual bool read(string_view name, float2x2& value) = 0;
	virtual bool read(string_view name, float3x3& value) = 0;
	virtual bool read(string_view name, float4x4& value) = 0;
	virtual bool read(string_view name, Aabb& value) = 0;
	virtual bool read(string_view name, Color& value) = 0;
	virtual bool read(string_view name, f32x4& value, uint8 components = 4) = 0;
	// TODO: read array of values.
};

/**
 * @brief Base serializable system interface.
 */
class ISerializable
{
public:
	/**
	 * @brief Prepares system for components serialization.
	 * @param[in,out] serializer target serializer instance
	 */
	virtual void preSerialize(ISerializer& serializer) { }
	/**
	 * @brief Serializes specified system component.
	 * 
	 * @param[in,out] serializer target serializer instance
	 * @param component system component view
	 */
	virtual void serialize(ISerializer& serializer, const View<Component> component) { }
	/**
	 * @brief Finalizes system after components serialization.
	 * @param[in,out] serializer target serializer instance
	 */
	virtual void postSerialize(ISerializer& serializer) { }

	/**
	 * @brief Prepares system for components deserialization.
	 * @param[in,out] deserializer target deserializer instance
	 */
	virtual void preDeserialize(IDeserializer& deserializer) { }
	/**
	 * @brief Deserializes specified system component.
	 * 
	 * @param[in,out] deserializer target deserializer instance
	 * @param component system component view
	 */
	virtual void deserialize(IDeserializer& deserializer, View<Component> component) { }
	/**
	 * @brief Finalizes system after components deserialization.
	 * @param[in,out] deserializer target deserializer instance
	 */
	virtual void postDeserialize(IDeserializer& deserializer) { }
};

/***********************************************************************************************************************
 * @brief Component indicating that this entity should not be serialized.
 * @details Useful in cases when we need to mark root or runtime entities.
 */
struct DoNotSerializeComponent : public Component { };

/**
 * @brief Handles entities that should not be serialized.
 */
class DoNotSerializeSystem : public ComponentSystem<DoNotSerializeComponent, false>, 
	public Singleton<DoNotSerializeSystem>
{
protected:
	/**
	 * @brief Creates a new DoNotSerialize system instance.
	 * @param setSingleton set system singleton instance
	 */
	DoNotSerializeSystem(bool setSingleton = true);
	/**
	 * @brief Destroys DoNotSerialize system instance.
	 */
	~DoNotSerializeSystem() override;

	string_view getComponentName() const override;
	friend class ecsm::Manager;
public:
	/**
	 * @brief Does the entity or its ancestors have a DoNotSerialize component.
	 * @param entity target entity with component
	 */
	bool hasOrAncestors(ID<Entity> entity) const;
};

} // namespace garden