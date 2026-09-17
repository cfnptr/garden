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
#include <vector>
#include <memory>

namespace garden
{

template <typename T, typename Allocator = std::allocator<T>>
struct no_init_allocator : public Allocator
{
	using traits = std::allocator_traits<Allocator>;

	template <typename U>
	struct rebind
	{
		using other = no_init_allocator<U, typename traits::template rebind_alloc<U>>;
	};

	template <typename U>
	void construct(U* ptr)
	{
		::new (static_cast<void*>(ptr)) U; 
	}

	template <typename U, typename... Args>
	void construct(U* ptr, Args&&... args)
	{
		traits::construct(static_cast<Allocator&>(*this), ptr, std::forward<Args>(args)...);
	}
};

template <typename T>
using raw_vector = std::vector<T, no_init_allocator<T>>;

}