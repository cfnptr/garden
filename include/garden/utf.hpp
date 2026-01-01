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
 * @brief Common UTF conversion functions.
 */

#pragma once
#include "garden/defines.hpp"

namespace garden
{

class UTF final
{
public:
	/**
	* @brief String containing all printable ASCII UTF-8 characters.
	*/
	static constexpr string_view printableAscii8 = 
		" !\"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_abcdefghijklmnopqrstuvwxyz{|}~";
	/**
	* @brief String containing all printable ASCII UTF-32 characters.
	*/
	static constexpr u32string_view printableAscii32 = 
		U" !\"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_abcdefghijklmnopqrstuvwxyz{|}~";

	/**
	* @brief Converts UTF-32 string to the UTF-8.
	* @return Zero on success, otherwise bad char index.
	* 
	* @param utf32 source UTF-32 string
	* @param[out] utf8 destination UTF-8 string 
	*/
	static psize convert(u32string_view utf32, string& utf8);
	/**
	* @brief Checks if specified UTF-8 encoded string is valid.
	* @return Zero on success, otherwise bad char index.
	* @param utf8 target UTF-8 string to validate
	*/
	static psize validate(string_view utf8);

	/**
	* @brief Converts UTF-8 string to the UTF-32.
	* @return Zero on success, otherwise bad char index.
	* 
	* @param utf8 source UTF-8 string
	* @param[out] utf32 destination UTF-32 string 
	*/
	static psize convert(string_view utf8, u32string& utf32);
	/**
	* @brief Checks if specified UTF-32 encoded string is valid.
	* @return Zero on success, otherwise bad char index.
	* @param[in] utf32 target UTF-32 string to validate
	*/
	static psize validate(u32string_view utf32);
};

} // namespace garden