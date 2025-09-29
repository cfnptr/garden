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
 * @brief Application entry point.
 */

#pragma once
#include "garden/defines.hpp"

#if GARDEN_OS_WINDOWS && !GARDEN_DEBUG
#include <windows.h>

/**
 * @brief Shows an OS error message with target C string.
 * @param[in] cstr target error message C string
 */
#define GARDEN_MESSAGE_ERROR(cstr) MessageBoxA(nullptr, cstr, "Error", MB_ICONERROR | MB_SYSTEMMODAL)
#else
/**
 * @brief Shows an OS error message with target C string.
 * @param[in] cstr target error message C string
 */
#define GARDEN_MESSAGE_ERROR(cstr) (void)0
#endif

#if GARDEN_OS_WINDOWS && !GARDEN_DEBUG && !defined(GARDEN_NO_GRAPHICS)
/**
 * @brief Defines application main function. (Entry point)
 */
#define GARDEN_MAIN int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
#else
/**
 * @brief Declares application main function.
 */
#define GARDEN_MAIN int main(int argc, char *argv[])
#endif

#if GARDEN_DEBUG
/**
 * @brief Declares application main function. (Entry point)
 * @param[in] entryPoint program entry point callback.
 */
#define GARDEN_DECLARE_MAIN(entryPoint) \
GARDEN_MAIN                             \
{                                       \
	entryPoint();                       \
	return EXIT_SUCCESS;                \
}
#else
/**
 * @brief Declares application main function. (Entry point)
 * @param[in] entryPoint program entry point callback.
 */
#define GARDEN_DECLARE_MAIN(entryPoint) \
GARDEN_MAIN                             \
{                                       \
	try                                 \
	{                                   \
		entryPoint();                   \
	}                                   \
	catch (const std::exception& e)     \
	{                                   \
		GARDEN_MESSAGE_ERROR(e.what()); \
		return EXIT_FAILURE;            \
	}                                   \
	return EXIT_SUCCESS;                \
}
#endif