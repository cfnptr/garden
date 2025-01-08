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

/**********************************************************************************************************************
 * @file
 * @brief Common Garden error (exception) functions.
 */

#pragma once
#include <string>
#include <exception>

namespace garden
{

using namespace std;

/**
 * @brief General Garden error (exception) class.
 */
class GardenError : public exception
{
protected:
	string message;
public:
	/**
	 * @brief Creates a new unspecified Garden error (exception) instance.
	 * @param message target error message
	 */
	GardenError() : message("Unspecified") { }
	/**
	 * @brief Creates a new Garden error (exception) instance. 
	 * @param message target error message
	 */
	GardenError(const string& message) : message(message) { }

	/**
	 * @brief Returns Garden error message C-string.
	 */
	const char* what() const noexcept override { return message.c_str(); }
	/**
	 * @brief Returns Garden error message string.
	 */
	const string& getMessage() const noexcept { return message; }
};

} // namespace garden