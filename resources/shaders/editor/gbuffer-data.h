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

#ifndef G_BUFFER_DATA_H
#define G_BUFFER_DATA_H

#define G_BUFFER_DRAW_MODE_OFF 0
#define G_BUFFER_DRAW_MODE_LIGHTING_DEBUG 1
#define G_BUFFER_DRAW_MODE_HDR_BUFFER 2
#define G_BUFFER_DRAW_MODE_DEPTH_BUFFER 3
#define G_BUFFER_DRAW_MODE_NORMAL_BUFFER 4
#define G_BUFFER_DRAW_MODE_REFLECTION_BUFFER 5
#define G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_COLOR 6
#define G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_ALPHA 7
#define G_BUFFER_DRAW_MODE_GLOBAL_AO 8
#define G_BUFFER_DRAW_MODE_GI_BUFFER 9
#define G_BUFFER_DRAW_MODE_OIT_ACCUM_COLOR 10
#define G_BUFFER_DRAW_MODE_OIT_ACCUM_ALPHA 11
#define G_BUFFER_DRAW_MODE_OIT_REVEAL 12

#define G_BUFFER_DRAW_MODE_BASE_COLOR 13
#define G_BUFFER_DRAW_MODE_SPECULAR_FACTOR 14
#define G_BUFFER_DRAW_MODE_METALLIC 15
#define G_BUFFER_DRAW_MODE_ROUGHNESS 16
#define G_BUFFER_DRAW_MODE_MATERIAL_AO 17
#define G_BUFFER_DRAW_MODE_REFLECTANCE 18
#define G_BUFFER_DRAW_MODE_MATERIAL_SHADOW 19
#define G_BUFFER_DRAW_MODE_EMISSIVE_COLOR 20
#define G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR 21
#define G_BUFFER_DRAW_MODE_CC_ROUGHNESS 22
#define G_BUFFER_DRAW_MODE_CC_NORMAL 23
#define G_BUFFER_DRAW_MODE_VELOCITY 24

#define G_BUFFER_DRAW_MODE_GLOBAL_BLURED_SHADOW_COLOR 25
#define G_BUFFER_DRAW_MODE_GLOBAL_BLURED_SHADOW_ALPHA 26
#define G_BUFFER_DRAW_MODE_GLOBAL_BLURED_AO 27
#define G_BUFFER_DRAW_MODE_WORLD_POSITION 28
#define G_BUFFER_DRAW_MODE_COUNT 29

#ifdef __GARDEN__
static const char* G_BUFFER_DRAW_MODE_NAMES[G_BUFFER_DRAW_MODE_COUNT] =
{
	"Off", "Lighting Debug", "HDR Buffer", "Depth Buffer", "Normal Buffer", 
	"Reflection Buffer", "Global Shadow Color", "Global Shadow Alpha", "Global AO", 
	"GI Buffer", "OIT Accum Color", "OI Accum Alpha", "OIT Revealage", 

	"Base Color", "Specular Factor", "Metallic", "Roughness", "Material AO", 
	"Reflectance", "Material Shadow", "Emissive Color", "Emissive Factor", 
	"Clear Coat Roughness", "Clear Coat Normal", "Velocity",
	

	"Global Blured Shadow Color", "Global Blured Shadow Alpha", 
	"Global Blured AO", "World Position"
};
#endif

#endif // G_BUFFER_DATA_H