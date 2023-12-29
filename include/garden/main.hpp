//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
#include "garden/defines.hpp"

#if _WIN32 && !GARDEN_DEBUG
#define NOMINMAX
#include <windows.h>
#define GARDEN_MAIN int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
#define GARDEN_MESSAGE_ERROR(cstr) MessageBoxA(NULL, cstr, "Error", MB_ICONERROR | MB_SYSTEMMODAL)
#else
#define GARDEN_MAIN int main(int argc, char *argv[])
#define GARDEN_MESSAGE_ERROR(cstr)
#endif