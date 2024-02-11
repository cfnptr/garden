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
 * @brief Target operating system.
 */

#pragma once
#include <cstdint>

#if _WIN32
#define GARDEN_OS_WINDOWS 1
#define GARDEN_OS_NAME "Windows"
#else
#define GARDEN_OS_WINDOWS 0
#endif

#if __APPLE__
#define GARDEN_OS_MACOS 1
#define GARDEN_OS_NAME "macOS"
#else
#define GARDEN_OS_MACOS 0
#endif

#if __linux__
#define GARDEN_OS_LINUX 1
#define GARDEN_OS_NAME "Linux"
#else
#define GARDEN_OS_LINUX 0
#endif

#if !GARDEN_OS_WINDOWS && !GARDEN_OS_MACOS && !GARDEN_OS_LINUX
#error Unknown operating system
#endif