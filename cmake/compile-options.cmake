if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Based on Steam survey instruction set support.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	add_compile_options(/MP /nologo)
	if(VOXFIELD_USE_AVX512)
		add_compile_options(/arch:AVX512)
	else()
		add_compile_options(/arch:AVX2)
	endif()
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	add_compile_options(-mfma)
	if(VOXFIELD_USE_AVX512)
		add_compile_options(-mavx512f -mavx512vl -mavx512dq -mavx2 -mbmi -mpopcnt -mlzcnt -mf16c)
	else()
		add_compile_options(-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c)
	endif()
endif()