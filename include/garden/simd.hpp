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

/***********************************************************************************************************************
 * @file
 * @brief Target SIMD support defines.
 * 
 * @details
 * SIMD stands for Single Instruction, Multiple Data. In SIMD processing, a single instruction is applied to multiple 
 * data elements, typically arranged in vectors or arrays. This allows for significant performance improvements in 
 * tasks that involve repetitive operations on large sets of data, such as image processing, audio/video processing, 
 * scientific simulations, and gaming.
 */

#pragma once
#include <cstdint>

#if __SSE__
#define GARDEN_SIMD_SSE 1
#else
#define GARDEN_SIMD_SSE 0
#endif

#if __SSE2__
#define GARDEN_SIMD_SSE2 1
#else
#define GARDEN_SIMD_SSE2 0
#endif

#if __SSE3__
#define GARDEN_SIMD_SSE3 1
#else
#define GARDEN_SIMD_SSE3 0
#endif

#if __SSSE3__
#define GARDEN_SIMD_SSSE3 1
#else
#define GARDEN_SIMD_SSSE3 0
#endif

#if __SSE4_1__
#define GARDEN_SIMD_SSE4_1 1
#else
#define GARDEN_SIMD_SSE4_1 0
#endif

#if __SSE4_2__
#define GARDEN_SIMD_SSE4_2 1
#else
#define GARDEN_SIMD_SSE4_2 0
#endif

#if __AVX__
#define GARDEN_SIMD_AVX 1
#else
#define GARDEN_SIMD_AVX 0
#endif

#if __AVX2__
#define GARDEN_SIMD_AVX2 1
#else
#define GARDEN_SIMD_AVX2 0
#endif

#if __FMA__
#define GARDEN_SIMD_FMA 1
#else
#define GARDEN_SIMD_FMA 0
#endif

#if __AVX512F__
#define GARDEN_SIMD_AVX512F 1
#else
#define GARDEN_SIMD_AVX512F 0
#endif

#if __AVX512VL__
#define GARDEN_SIMD_AVX512VL 1
#else
#define GARDEN_SIMD_AVX512VL 0
#endif

#if __AVX512DQ__
#define GARDEN_SIMD_AVX512DQ 1
#else
#define GARDEN_SIMD_AVX512DQ 0
#endif

#if __ARM_NEON
#define GARDEN_SIMD_NEON 1
#else
#define GARDEN_SIMD_NEON 0
#endif

//**********************************************************************************************************************
constexpr const char* GARDEN_SIMD_STRING = ""
	#if GARDEN_SIMD_SSE
	"SSE "
	#endif
	#if GARDEN_SIMD_SSE2
	"SSE2 "
	#endif
	#if GARDEN_SIMD_SSE3
	"SSE3 "
	#endif
	#if GARDEN_SIMD_SSSE3
	"SSSE3 "
	#endif
	#if GARDEN_SIMD_SSE4_1
	"SSE4_1 "
	#endif
	#if GARDEN_SIMD_SSE4_2
	"SSE4_2 "
	#endif
	#if GARDEN_SIMD_AVX
	"AVX "
	#endif
	#if GARDEN_SIMD_AVX2
	"AVX2 "
	#endif
	#if GARDEN_SIMD_FMA
	"FMA "
	#endif
	#if GARDEN_SIMD_AVX512F
	"AVX512F "
	#endif
	#if GARDEN_SIMD_AVX512VL
	"AVX512VL "
	#endif
	#if GARDEN_SIMD_AVX512DQ
	"AVX512DQ "
	#endif
	#if GARDEN_SIMD_NEON
	"NEON "
	#endif
;