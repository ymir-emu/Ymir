cmake_minimum_required(VERSION 3.28)
project(Ymir VERSION 0.4.0)

# Check for Vulkan support before including CompileShaders as it depends on knowing if Vulkan
# is available to determine whether to make SPIR-V compilation required or optional.
# Vulkan support is excluded from macOS as the system doesn't support it natively.
# Metal is the preferred backend on that platform.
if (NOT APPLE)
    find_package(Vulkan COMPONENTS SPIRV-Tools)
endif ()

include(cmake/CMakeRC.cmake)
include(cmake/CompileShaders.cmake)
include(cmake/CopyRuntimeDLLs.cmake)
include(cmake/TargetConditionalSources.cmake)
include(CMakeDependentOption)

# CMake project structured adapted from Alex Reinking's SharedStaticStarter
# GitHub: https://github.com/alexreinking/SharedStaticStarter
# Blog: https://alexreinking.com/blog/building-a-dual-shared-and-static-library-with-cmake.html

## Determine if this is a top-level build or included as a subproject
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" is_top_level)

option(Ymir_DEV_BUILD "Mark as a development build" ON)
option(Ymir_EXTRA_INLINING "Enable more aggressive inlining (slows down build!)" "${is_top_level}")
option(Ymir_ENABLE_TESTS "Enable tests for Ymir" "${is_top_level}")
option(Ymir_ENABLE_SANDBOX "Compile the sandbox app" "${is_top_level}")
option(Ymir_ENABLE_YMDASM "Compile the disassembly tool" "${is_top_level}")
option(Ymir_ENABLE_IPO "Enable IPO / LTO for Ymir" ON)
option(Ymir_ENABLE_DEVLOG "Enable development logs" ${Ymir_DEV_BUILD})
option(Ymir_ENABLE_DEV_ASSERTIONS "Enable development-time assertions" OFF)
option(Ymir_ENABLE_IMGUI_DEMO "Enable ImGui demo window" ON)
option(Ymir_ENABLE_UPDATE_CHECKS "Enable update checks" ON)
cmake_dependent_option(Ymir_LIBRARY_ONLY "Compile ymir-core only" OFF is_top_level ON)

## Feature flags
## If ON, all feature flags will be enabled by default
## All feature flags can be selectively overridden
option(Ymir_FEATUREFLAG_DEFAULT "Enable all feature flags by default" ${Ymir_DEV_BUILD})

## Selectively enable in-development features
## Template:
## option(Ymir_FF_[NAME] "[description]" ${Ymir_FEATUREFLAG_DEFAULT})
option(Ymir_FF_HOST_CD_DRIVES "Enable support for reading discs from host drives" OFF)

if (Ymir_LIBRARY_ONLY)
    message(STATUS "Ymir: Library-only build")
endif ()
if (Ymir_DEV_BUILD)
    message(STATUS "Ymir: Development build")
else ()
    message(STATUS "Ymir: Release build")
endif ()
message(STATUS "Ymir: Devlog ${Ymir_ENABLE_DEVLOG}")
message(STATUS "Ymir: Extra inlining ${Ymir_EXTRA_INLINING}")
message(STATUS "Ymir: Update checks ${Ymir_ENABLE_UPDATE_CHECKS}")

message(STATUS "Ymir: Feature flags:")
#message(STATUS "- [Name]: ${Ymir_FF_[NAME]}")
message(STATUS "- Host CD drives: ${Ymir_FF_HOST_CD_DRIVES}")

# Create Universal Binary on MacOS
#if (APPLE)
#    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
#endif ()

# Detect target architectures
# ARCHITECTURES may have multiple variables in the case of a multi-arch build
# such as a universal binary on MacOS
include(cmake/DetectArchitecture.cmake)
if (NOT DEFINED ARCHITECTURES)
    message(FATAL_ERROR "Unsupported architecture")
endif ()
message(STATUS "Ymir: Architecture(s) ${ARCHITECTURES}")

## Performance options
if ("x86_64" IN_LIST ARCHITECTURES)
	option(Ymir_AVX2 "Enable AVX2 instruction set for Ymir" OFF)
	if (Ymir_AVX2)
		message(STATUS "Ymir: Using AVX2 instruction set")
	else ()
		message(STATUS "Ymir: Using SSE2 instruction set")
	endif ()
endif ()

if ("arm64" IN_LIST ARCHITECTURES)
	message(STATUS "Ymir: Using NEON instruction set")
endif ()


## C++ language configuration boilerplate
if (NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET AND
    NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
endif ()

## Force static linkage
set(Ymir_SHARED_LIBS OFF)
set(BUILD_SHARED_LIBS OFF)

## Let Ymir_SHARED_LIBS override BUILD_SHARED_LIBS
#if (DEFINED Ymir_SHARED_LIBS)
#    set(BUILD_SHARED_LIBS "${Ymir_SHARED_LIBS}")
#endif ()

## Build static libraries by default if BUILD_SHARED_LIBS is not specified
#if (NOT DEFINED BUILD_SHARED_LIBS)
#    set(BUILD_SHARED_LIBS OFF)
#endif ()

## Configure Visual Studio solution
if (MSVC)
    include(cmake/VSHelpers.cmake)

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif ()

## Enable LTO if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

if (IPO_SUPPORTED)
    message(STATUS "IPO / LTO supported")
else ()
    message(STATUS "IPO / LTO not supported: ${IPO_ERROR}")
endif ()

## Include third-party libraries
add_subdirectory(vendor)

## Configure PGO (apply after vendor so third-party libs don't inherit flags)
include(cmake/PGO.cmake)
ymir_configure_pgo()

## Include project libraries
add_subdirectory(libs)

if (NOT Ymir_LIBRARY_ONLY)
    ## Include apps
    add_subdirectory(apps)

    ## Include the unit tests if the user wanted them (included by default when top-level)
    if (Ymir_ENABLE_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif ()

    ## Configure Visual Studio solution
    if (MSVC)
        set_property(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" PROPERTY VS_STARTUP_PROJECT ymir-sdl3)
    endif ()
endif ()
