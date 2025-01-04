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

/**********************************************************************************************************************
 * @file
 * @brief Target CPU architecture defines.
 */

#pragma once

#if defined(__x86_64__) || defined(_M_X64)
#define GARDEN_CPU_ARCH "x86-64"
#define GARDEN_CPU_ARCH_X86_64
#define GARDEN_CPU_ARCH_X86
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define GARDEN_CPU_ARCH "x86-32"
#define GARDEN_CPU_ARCH_X86_32
#define GARDEN_CPU_ARCH_X86
#elif defined(__aarch64__) || defined(_M_ARM64)
#define GARDEN_CPU_ARCH "ARM64"
#define GARDEN_CPU_ARCH_ARM64
#define GARDEN_CPU_ARCH_ARM
#elif defined(__arm__) || defined(_M_ARM)
#define GARDEN_CPU_ARCH "ARM32"
#define GARDEN_CPU_ARCH_ARM32
#define GARDEN_CPU_ARCH_ARM
#else
#error "Unknown CPU architecture"
#endif