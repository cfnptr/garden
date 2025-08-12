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
	f32x4x4 view = f32x4x4::zero;         /**< Camera view matrix. */
	f32x4x4 projection = f32x4x4::zero;   /**< Camera projection matrix. */
	f32x4x4 viewProj = f32x4x4::zero;     /**< Camera view * projection matrix. */
	f32x4x4 inverseView = f32x4x4::zero;  /**< Camera inverse view matrix. */
	f32x4x4 inverseProj = f32x4x4::zero;  /**< Camera inverse projection matrix. */
	f32x4x4 invViewProj = f32x4x4::zero;  /**< Camera inverse (view * projection) matrix. */
	f32x4 cameraPos = f32x4::zero;        /**< Camera position in world space. */
	f32x4 giBufferPos = f32x4::zero;      /**< Global illumination buffer position in world space. */
	f32x4 viewDir = f32x4::zero;          /**< Camera view direction in world space. */
	f32x4 lightDir = f32x4::zero;         /**< Light direction in world space. (From sun to world) */
	f32x4 shadowColor = f32x4::zero;      /**< Shadow color and intensity. */
	f32x4 skyColor = f32x4::zero;         /**< Sky color and intensity.*/
	float2 frameSize = float2::zero;      /**< Frame size in pixels. */
	float2 invFrameSize = float2::zero;   /**< Inverse frame size in pixels. */
	float2 invFrameSizeSq = float2::zero; /**< Inverse frame size * 2 in pixels. */
	float nearPlane = 0.0f;               /**< Near frustum plane. */
	float currentTime = 0.0f;             /**< Time since start of the program. (In seconds) */
	float deltaTime = 0.0f;               /**< Time elapsed between two previous frames. (In seconds) */
	float emissiveCoeff = 0.0f;           /**< Maximum brightness coefficient. */
	float anglePerPixel = 0.0f;           /**< Vertical field-of-view per pixel. */
	float ggxLodOffset = 0.0f;            /**< Spherical GGX distribution blur LOD offset. */
};

} // namespace garden::graphics
