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

#pragma once
#include "math/matrix.hpp"

namespace garden::graphics
{

using namespace math;

/**
 * @brief Common constants shared across graphics systems.
 */
struct CommonConstants final
{
	f32x4x4 view = f32x4x4::zero;           /**< Camera view matrix. */
	f32x4x4 projection = f32x4x4::zero;     /**< Camera projection matrix. */
	f32x4x4 viewProj = f32x4x4::zero;       /**< Camera view * projection matrix. */
	f32x4x4 inverseView = f32x4x4::zero;    /**< Camera inverse view matrix. */
	f32x4x4 inverseProj = f32x4x4::zero;    /**< Camera inverse projection matrix. */
	f32x4x4 invViewProj = f32x4x4::zero;    /**< Camera inverse (view * projection) matrix. */
	f32x4x4 prevViewProj = f32x4x4::zero;   /**< Camera previous frame (view * projection) matrix. */
	f32x4 shadowColor = f32x4::zero;        /**< Shadow color and intensity. */
	float3 cameraPos = float3::zero;        /**< Camera position in world space. */
	float nearPlane = 0.0f;                 /**< Camera near frustum plane. */
	float3 giBufferPos = float3::zero;      /**< Global illumination buffer position in world space. */
	float currentTime = 0.0f;               /**< Time since start of the program. (In seconds) */
	float3 viewDir = float3::zero;          /**< Camera view direction in world space. */
	float deltaTime = 0.0f;                 /**< Time elapsed between two previous frames. (In seconds) */
	float3 lightDir = float3::zero;         /**< Light direction in world space. (From star to world) */
	float emissiveCoeff = 0.0f;             /**< Maximum brightness coefficient. */
	float3 starLight = float3::zero;        /**< Nearby star (Sun) light color. (Energy) */
	float anglePerPixel = 0.0f;             /**< Vertical field-of-view per pixel. */
	float3 ambientLight = float3::zero;     /**< World ambient light color. (Energy) */
	float mipLodBias = 0.0f;                /**< Preferred mip-map LOD bias. */
	float3 upDir = float3::zero;            /**< World up direction. (Inversed gravityDir) */
	float ggxLodOffset = 0.0f;              /**< Spherical GGX distribution blur LOD offset. */
	float2 frameSize = float2::zero;        /**< Frame size in pixels. */
	float2 invFrameSize = float2::zero;     /**< Inverse frame size in pixels. */
	float2 invFrameSizeSq = float2::zero;   /**< Inverse frame size * 2 in pixels. */
	float2 jitterOffset = float2::zero;     /**< Frame sub-pixel jittering offsets. */
	float2 prevJitterOffset = float2::zero; /**< Previous frame sub-pixel jittering offsets. */
	float3 windDir = float3::zero;          /**< Direction of the wind. (Vector) */
};

} // namespace garden::graphics
