################################################################################
## Find required tools.
##
## Targets:
##   Direct3D 12 - DXIL
##   Vulkan - SPIR-V
##   Metal - SPIR-V -> MetalLib
##
## Requires DXC (DirectX Shader Compiler) on all platforms.
## glslc (shaderc) cannot be used as it lacks support for several modern HLSL
## features, including 64-bit integers, that some shaders rely on.
##
## When locating DXC, this script prefers the compiler included with Vulkan SDK
## as it supports SPIR-V. It falls back to system-provided DXC otherwise, such
## as Visual Studio's DXC which only targets DXIL (in which case the lack of
## SPIR-V support doesn't matter).
##
## On macOS, DXC with SPIR-V support is required for the first compilation step.
## Later steps require metal and metallib.
##
## Outputs:
##   DXC_EXECUTABLE (STRING): path to DXC executable
##   DXC_DXIL_SUPPORTED (BOOL): whether the DXC executable can output DXIL
##   DXC_SPIRV_SUPPORTED (BOOL): whether the DXC executable can output SPIR-V

# Try to locate DXC in Vulkan SDK.
find_program(DXC_EXECUTABLE_VULKAN
    NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/bin64"
    NO_DEFAULT_PATH
)

# If found, use it.
if (DXC_EXECUTABLE_VULKAN)
    set(DXC_EXECUTABLE ${DXC_EXECUTABLE_VULKAN})
endif ()

# Fall back to system-provided DXC compiler.
if (NOT DXC_EXECUTABLE)
    find_program(DXC_EXECUTABLE NAMES dxc)
endif ()

# Bail out right away on Windows as we cannot easily compile DXIL shaders without DXC.
if (WIN32 AND NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "Could NOT find DXC. Cannot compile DXIL shaders.")
endif ()

# DXC always supports DXIL code generation, but we only enable it on Windows because that's
# the only system in which it makes sense to target DXIL.
set(DXC_DXIL_SUPPORTED ${WIN32})

# Check if DXC supports SPIR-V code generation.
set(DXC_SPIRV_SUPPORTED OFF)
if (DXC_EXECUTABLE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/__dxc_spirv_test.hlsl" "
        [shader(\"compute\")]
        [numthreads(1, 1, 1)]
        void main() {}
    ")
    execute_process(
        COMMAND ${DXC_EXECUTABLE} -spirv -T cs_6_0 -E main "${CMAKE_CURRENT_BINARY_DIR}/__dxc_spirv_test.hlsl"
        OUTPUT_VARIABLE DXC_STDOUT
        ERROR_VARIABLE DXC_STDERR
        RESULT_VARIABLE DXC_EXIT_CODE
    )
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/__dxc_spirv_test.hlsl")
    if (${DXC_EXIT_CODE} EQUAL 0)
        set(DXC_SPIRV_SUPPORTED ON)
    else ()
        set(DXC_SPIRV_SUPPORTED OFF)
    endif ()
endif ()

# SPIR-V is required on systems other than Windows if Vulkan is supported. Bail out if that's not the case.
if (NOT WIN32 AND Vulkan_FOUND AND NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "Could NOT find a shader compiler supporting SPIR-V. Cannot compile shaders.")
endif ()

if (APPLE)
    find_program(SPIRV_CROSS_EXECUTABLE NAMES spirv-cross)
    if (NOT SPIRV_CROSS_EXECUTABLE)
        message(FATAL_ERROR "Could NOT find spirv-cross. Cannot compile shaders for Metal.")
    endif ()

    find_program(XCRUN_EXECUTABLE NAMES xcrun)
    if (XCRUN_EXECUTABLE)
        execute_process(
            COMMAND ${XCRUN_EXECUTABLE} -find metal
            OUTPUT_VARIABLE METAL_EXECUTABLE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND ${XCRUN_EXECUTABLE} -find metallib
            OUTPUT_VARIABLE METALLIB_EXECUTABLE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif ()
    if (NOT METAL_EXECUTABLE OR NOT METALLIB_EXECUTABLE)
        find_program(METAL_EXECUTABLE NAMES metal)
        find_program(METALLIB_EXECUTABLE NAMES metallib)
    endif ()
    if (NOT METAL_EXECUTABLE OR NOT METALLIB_EXECUTABLE)
        message(FATAL_ERROR "Could NOT find metal and metallib. Cannot compile shaders for Metal.")
    endif ()
endif ()

# Output status
if (DXC_EXECUTABLE)
    message(STATUS "DXC found: ${DXC_EXECUTABLE}")
    if (DXC_DXIL_SUPPORTED)
        message(STATUS "DXC supports DXIL shaders")
    endif ()
    if (DXC_SPIRV_SUPPORTED)
        message(STATUS "DXC supports SPIR-V shaders")
    endif ()
else ()
    message(STATUS "DXC not found. No shaders will be compiled.")

    # Define dummy no-op function to avoid breaking builds without Vulkan support
    function(compile_shader)
    endfunction()

    return()
endif ()

################################################################################
## Helper functions

# _shader_get_glslc_args_for_profile(
#     OUT_STAGE <variable>
#     OUT_TARGET_ENV <variable>
#     PROFILE <string>
# )
#
# Retrieves roughly compatible glslc command line arguments for the specified
# Shader Model profile.
#
# Shader Model prefixes are converted to equivalent shader stages as follows:
#   vs -> vertex
#   ps -> fragment
#   cs -> compute
#
# Shader Model versions determine the target environment:
#
#  6_0 to 6_1 -> vulkan1.1
#  6_2 to 6_4 -> vulkan1.2
#  6_5 to 6_7 -> vulkan1.3
#  6_8 and up -> vulkan1.4
#
# Parameters:
#
#   OUT_STAGE
#       Name of a variable in the parent scope that will receive the equivalent
#       glslc stage parameter.
#
#   OUT_TARGET_ENV
#       Name of a variable in the parent scope that will receive the equivalent
#       glslc target environment parameter.
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
# Output:
#   OUT_STAGE and OUT_TARGET_ENV are set to the equivalent values for glslc's
#   -fshader-stage and --target-env command line options.
function(_shader_get_glslc_args_for_profile)
    # Parse arguments
    set(options)
    set(oneValueArgs
        OUT_STAGE
        OUT_TARGET_ENV
        PROFILE
    )
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (${ARG_PROFILE} MATCHES "^(vs|ps|cs)_([0-9]+)_([0-9]+)$")
        if (${CMAKE_MATCH_1} STREQUAL "vs")
            set(${ARG_OUT_STAGE} "vertex" PARENT_SCOPE)
        elseif (${CMAKE_MATCH_1} STREQUAL "ps")
            set(${ARG_OUT_STAGE} "fragment" PARENT_SCOPE)
        elseif (${CMAKE_MATCH_1} STREQUAL "cs")
            set(${ARG_OUT_STAGE} "compute" PARENT_SCOPE)
        else ()
            message(FATAL_ERROR "Unexpected PROFILE shader stage. Expected one of [vs, ps, cs].")
        endif ()

        if (${CMAKE_MATCH_2} STREQUAL "6")
            if (${CMAKE_MATCH_2} GREATER_EQUAL "0" AND ${CMAKE_MATCH_2} LESS_EQUAL "1")
                set(${ARG_OUT_TARGET_ENV} "vulkan1.1" PARENT_SCOPE)
            elseif (${CMAKE_MATCH_2} GREATER_EQUAL "2" AND ${CMAKE_MATCH_2} LESS_EQUAL "4")
                set(${ARG_OUT_TARGET_ENV} "vulkan1.2" PARENT_SCOPE)
            elseif (${CMAKE_MATCH_2} GREATER_EQUAL "5" AND ${CMAKE_MATCH_2} LESS_EQUAL "7")
                set(${ARG_OUT_TARGET_ENV} "vulkan1.3" PARENT_SCOPE)
            elseif (${CMAKE_MATCH_2} GREATER_EQUAL "8")
                set(${ARG_OUT_TARGET_ENV} "vulkan1.4" PARENT_SCOPE)
            else ()
                message(FATAL_ERROR "Unexpected PROFILE version. Expected 6_x.")
            endif ()
        else ()
            message(FATAL_ERROR "Unexpected PROFILE version. Expected 6_x.")
        endif ()
    else ()
        message(FATAL_ERROR
            "Invalid PROFILE argument syntax. "
            "Expected a valid Shader Model specification, such as vs_6_0."
        )
    endif ()
endfunction()

# _shader_make_generate_depfile_command(
#     OUT_COMMAND <variable>
#     SOURCE <path_to_hlsl>
#     DEPFILE <path_to_depfile>
#     ENTRYPOINT <string>
#     PROFILE <string>
#     [INCLUDE_PATHS <path1> <path2> ...]
#     [MACROS <macro1> <macro2> ...]
# )
#
# Outputs a COMMAND argument for use with add_custom_command that generates the
# dependency list for the given HLSL source file using either DXC or glslc,
# whichever is available on the system.
#
# Parameters:
#
#   OUT_COMMAND
#       Name of a variable in the parent scope that will receive the COMMAND
#       argument for use with add_custom_command to generate the dependency
#       list file.
#
#   SOURCE
#       Absolute path to the HLSL shader source file.
#
#   DEPFILE
#       Absolute path to the dependency list file to be written.
#
#   ENTRYPOINT
#       Shader entry point function name (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   INCLUDE_PATHS (optional)
#       List of include paths.
#
#   MACROS (optional)
#       List of preprocessor defines to pass to the shader.
#
# Output:
#   OUT_COMMAND is set to a COMMAND argument that produces the dependency list
#   for the given HLSL source file.
function(_shader_make_generate_depfile_command)
    # Parse arguments
    set(options)
    set(oneValueArgs
        OUT_COMMAND
        SOURCE
        DEPFILE
        ENTRYPOINT
        PROFILE
    )
    set(multiValueArgs
        INCLUDE_PATHS
        MACROS
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    list(TRANSFORM ARG_INCLUDE_PATHS PREPEND "-I" OUTPUT_VARIABLE _include_paths_args)
    list(TRANSFORM ARG_MACROS PREPEND "-D" OUTPUT_VARIABLE _macro_args)

    if (DXC_EXECUTABLE)
        set(_command COMMAND "${DXC_EXECUTABLE}"
            "-MD" "-MF" "${ARG_DEPFILE}"
            "-T" "${ARG_PROFILE}"
            "-E" "${ARG_ENTRYPOINT}"
            ${_include_paths_args}
            ${_macro_args}
            "${ARG_SOURCE}"
        )
        set(${ARG_OUT_COMMAND} ${_command} PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "DXC is not available. Cannot generate dependency list command.")
    endif ()
endfunction()

# _shader_make_compile_dxil_command(
#     OUT_COMMAND <variable>
#     SOURCE <path_to_hlsl>
#     DESTINATION <path_to_output_file>
#     ENTRYPOINT <string>
#     PROFILE <string>
#     [INCLUDE_PATHS <path1> <path2> ...]
#     [MACROS <macro1> <macro2> ...]
#     [INCLUDE_REFLECTION]
# )
#
# Outputs a COMMAND argument for use with add_custom_command that compiles the
# given HLSL shader source code into a DXIL binary at the destination path with
# the specified configuration.
#
# If DXIL is not supported, OUT_COMMAND is set to an empty list.
#
# Parameters:
#
#   OUT_COMMAND
#       Name of a variable in the parent scope that will receive the COMMAND
#       argument for use with add_custom_command to compile the shader.
#
#   SOURCE
#       Absolute path to the HLSL shader source file.
#
#   DESTINATION
#       Absolute path to the output binary file to be written.
#
#   ENTRYPOINT
#       Shader entry point function name (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   INCLUDE_PATHS (optional)
#       List of include paths.
#
#   MACROS (optional)
#       List of preprocessor defines to pass to the shader.
#
#   INCLUDE_REFLECTION (optional)
#       If specified, compiles the shaders with reflection information.
#
# Output:
#   OUT_COMMAND is set to a COMMAND argument that compiles the given HLSL source
#   file into a DXIL binary at the destination path.
function(_shader_make_compile_dxil_command)
    # Parse arguments
    set(options
        INCLUDE_REFLECTION
    )
    set(oneValueArgs
        OUT_COMMAND
        SOURCE
        DESTINATION
        ENTRYPOINT
        PROFILE
    )
    set(multiValueArgs
        INCLUDE_PATHS
        MACROS
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    list(TRANSFORM ARG_INCLUDE_PATHS PREPEND "-I" OUTPUT_VARIABLE _include_paths_args)
    list(TRANSFORM ARG_MACROS PREPEND "-D" OUTPUT_VARIABLE _macro_args)

    if (DXC_DXIL_SUPPORTED)
        set(_compile_flags "")
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND _compile_flags "-Od" "-Qembed_debug" "-Zi")
        else ()
            list(APPEND _compile_flags "-O3" "-Qstrip_debug")
        endif ()
        if (NOT ARG_INCLUDE_REFLECTION)
            list(APPEND _compile_flags "-Qstrip_reflect")
        endif ()

        set(${ARG_OUT_COMMAND}
            COMMAND "${DXC_EXECUTABLE}"
                -T "${ARG_PROFILE}"
                -E "${ARG_ENTRYPOINT}"
                ${_compile_flags}
                ${_include_paths_args}
                ${_macro_args}
                -Fo "${ARG_DESTINATION}"
                "${ARG_SOURCE}"
            PARENT_SCOPE
        )
    else ()
        set(${ARG_OUT_COMMAND} "" PARENT_SCOPE)
    endif ()
endfunction()

# _shader_make_compile_spirv_command(
#     OUT_COMMAND <variable>
#     SOURCE <path_to_hlsl>
#     DESTINATION <path_to_output_file>
#     ENTRYPOINT <string>
#     PROFILE <string>
#     [INCLUDE_PATHS <path1> <path2> ...]
#     [MACROS <macro1> <macro2> ...]
#     [INCLUDE_REFLECTION]
# )
#
# Outputs a COMMAND argument for use with add_custom_command that compiles the
# given HLSL shader source code into a SPIR-V binary at the destination path
# with the specified configuration.
#
# If SPIR-V isn't supported, OUT_COMMAND is set to an empty list.
#
# Parameters:
#
#   OUT_COMMAND
#       Name of a variable in the parent scope that will receive the COMMAND
#       argument for use with add_custom_command to compile the shader.
#
#   SOURCE
#       Absolute path to the HLSL shader source file.
#
#   DESTINATION
#       Absolute path to the output binary file to be written.
#
#   ENTRYPOINT
#       Shader entry point function name (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   INCLUDE_PATHS (optional)
#       List of include paths.
#
#   MACROS (optional)
#       List of preprocessor defines to pass to the shader.
#
#   INCLUDE_REFLECTION (optional)
#       If specified, compiles the shaders with reflection information.
#
# Output:
#   OUT_COMMAND is set to a COMMAND argument that compiles the given HLSL source
#   file into a SPIR-V binary at the destination path.
function(_shader_make_compile_spirv_command)
    # Parse arguments
    set(options
        INCLUDE_REFLECTION
    )
    set(oneValueArgs
        OUT_COMMAND
        SOURCE
        DESTINATION
        ENTRYPOINT
        PROFILE
    )
    set(multiValueArgs
        INCLUDE_PATHS
        MACROS
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    list(TRANSFORM ARG_INCLUDE_PATHS PREPEND "-I" OUTPUT_VARIABLE _include_paths_args)
    list(TRANSFORM ARG_MACROS PREPEND "-D" OUTPUT_VARIABLE _macro_args)

    if (DXC_SPIRV_SUPPORTED)
        set(_compile_flags "")
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND _compile_flags "-fspv-debug=vulkan-with-source")
        endif ()
        if (ARG_INCLUDE_REFLECTION)
            list(APPEND _compile_flags "-fspv-reflect")
        endif ()

        set(${ARG_OUT_COMMAND}
            COMMAND "${DXC_EXECUTABLE}"
                -T "${ARG_PROFILE}"
                -E "${ARG_ENTRYPOINT}"
                -spirv
                ${_compile_flags}
                ${_include_paths_args}
                ${_macro_args}
                -Fo "${ARG_DESTINATION}"
                "${ARG_SOURCE}"
            PARENT_SCOPE
        )
    else ()
        set(${ARG_OUT_COMMAND} "" PARENT_SCOPE)
    endif ()
endfunction()

# _shader_make_compile_metal_command(
#     OUT_COMMAND <variable>
#     SPIRV_SOURCE <path_to_spv>
#     METAL_DESTINATION <path_to_output_metal>
#     AIR_DESTINATION <path_to_output_air>
#     METALLIB_DESTINATION <path_to_output_metallib>
#     ENTRYPOINT <string>
#     PROFILE <string>
# )
function(_shader_make_compile_metal_command)
    set(options)
    set(oneValueArgs
        OUT_COMMAND
        SPIRV_SOURCE
        METAL_DESTINATION
        AIR_DESTINATION
        METALLIB_DESTINATION
        ENTRYPOINT
        PROFILE
    )
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (APPLE AND SPIRV_CROSS_EXECUTABLE AND METAL_EXECUTABLE AND METALLIB_EXECUTABLE)
        set(_metal_compile_flags "")
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND _metal_compile_flags "-gline-tables-only" "-MO")
        else ()
            list(APPEND _metal_compile_flags "-O3")
        endif ()

        set(${ARG_OUT_COMMAND}
            COMMAND "${SPIRV_CROSS_EXECUTABLE}"
                "${ARG_SPIRV_SOURCE}"
                --msl
                --msl-version 20200  # for 64-bit integers
                --output "${ARG_METAL_DESTINATION}"
            COMMAND "${METAL_EXECUTABLE}"
                ${_metal_compile_flags}
                -c "${ARG_METAL_DESTINATION}"
                -o "${ARG_AIR_DESTINATION}"
            COMMAND "${METALLIB_EXECUTABLE}"
                "${ARG_AIR_DESTINATION}"
                -o "${ARG_METALLIB_DESTINATION}"
            PARENT_SCOPE
        )
    else ()
        set(${ARG_OUT_COMMAND} "" PARENT_SCOPE)
    endif ()
endfunction()

################################################################################
## Compiler function

# compile_shader(
#     OUT_PATHS <variable>
#     SOURCE <path_to_hlsl>
#     [WHENCE <base_path>]
#     DESTINATION <directory>
#     ENTRYPOINT <string>
#     PROFILE <string>
#     [VARIANT <string>]
#     [INCLUDE_PATHS <path1> <path2> ...]
#     [MACROS <macro1> <macro2> ...]
#     [INCLUDE_REFLECTION]
# )
#
# Configures a custom target to build a single HLSL shader with DXC, producing
# DXIL and/or SPIR-V bytecode for Direct3D 12 and Vulkan respectively depending
# on the target platform.
#
# On Ninja/Makefile generators, all dependent source files (#includes) will also
# trigger shader recompilation. On other generators, dependencies are tracked at
# CMake generation time. Changes to shader include relationships (adding or
# removing #include directives) require reconfiguring the CMake project to
# update the dependency graph. Shader recompilation still works correctly when
# existing dependency files change.
#
# Compiler optimization flags are automatically set depending on the build type.
# For Debug builds, shaders are compiled with no optimizations and include all
# debug information as well as the shader source code. For Release builds of any
# kind, the maximum optimization level is enabled and all debug information is
# stripped from the shader blob. In all cases, no reflection data is included
# unless INCLUDE_REFLECTION is specified.
#
# Parameters:
#
#   OUT_PATHS
#       Name of a variable in the parent scope that will receive the full path
#       to the generated .cso and/or .spv files.
#
#   SOURCE
#       Path to the HLSL shader source file.
#
#   WHENCE
#       Base path to HLSL shader files. Assumes ${CMAKE_CURRENT_SOURCE_DIR} by
#       default if omitted.
#
#   DESTINATION
#       Directory where the compiled .cso/.spv and generated .d files will be
#       written.
#
#   ENTRYPOINT
#       Shader entry point function name (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   VARIANT (optional)
#       Appends a suffix to the base filename (before the extension) to produce
#       a shader variant. This is useful when building multiple versions of the
#       same shader using different macro sets.
#
#   INCLUDE_PATHS (optional)
#       List of include paths.
#
#   MACROS (optional)
#       List of preprocessor defines to pass to the shader.
#
#   INCLUDE_REFLECTION (optional)
#       If specified, shaders are compiled with reflection information.
#
# Output:
#   OUT_PATHS is set to a list of full paths of the generated .cso/.spv files.
function(compile_shader)
    # Parse arguments
    set(options
        INCLUDE_REFLECTION
    )
    set(oneValueArgs
        OUT_PATHS
        SOURCE
        WHENCE
        DESTINATION
        ENTRYPOINT
        PROFILE
        VARIANT
    )
    set(multiValueArgs
        INCLUDE_PATHS
        MACROS
    )
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Set up option forwarding
    foreach (_option ${options})
        set(_fwd_${_option} "")
        if (${ARG_${_option}})
            set(_fwd_${_option} ${_option})
        endif ()
    endforeach ()

    # Extract paths.
    #
    # WHENCE specifies the root path for shaders.
    # SOURCE is the path to the shader relative to the current source directory.
    # Break those down into:
    #   _base_path: path to the file relative to WHENCE, used for the virtual file system structure
    #   _base_name: file name without path or extension so we can turn <file>.hlsl into <file>.cso/spv/d
    if (NOT ARG_WHENCE)
        set(ARG_WHENCE ${CMAKE_CURRENT_SOURCE_DIR})
    endif ()
    get_filename_component(_source_abs_path "${ARG_SOURCE}" ABSOLUTE)
    file(RELATIVE_PATH _source_rel_path "${ARG_WHENCE}" "${_source_abs_path}")
    get_filename_component(_base_path "${_source_rel_path}" DIRECTORY)
    get_filename_component(_base_name "${_source_rel_path}" NAME_WE)
    if (_source_rel_path MATCHES "^\\.\\.")
        # Require files to be relative to the root directory
        message(SEND_ERROR "Cannot add file ${ARG_SOURCE}: File must be in a subdirectory of ${ARG_WHENCE}")
        continue()
    endif ()

    # Apply variant name suffix if specified
    if (NOT "${ARG_VARIANT}" STREQUAL "")
        set(_final_name "${_base_name}_${ARG_VARIANT}")
    else ()
        set(_final_name "${_base_name}")
    endif ()

    set(_dep_file "${ARG_DESTINATION}/${_base_path}/${_final_name}.d")
    set(_out_shader_path "${ARG_DESTINATION}/${_base_path}/${_final_name}")
    set(_out_dxil_path "${_out_shader_path}.cso")
    set(_out_spirv_path "${_out_shader_path}.spv")

    # Set up commands and outputs
    set(_compile_commands "")
    set(_outputs "")

    # Make command: generate dependency file
    _shader_make_generate_depfile_command(
        OUT_COMMAND _depfile_command
        SOURCE "${_source_abs_path}"
        DEPFILE "${_dep_file}"
        ENTRYPOINT ${ARG_ENTRYPOINT}
        PROFILE ${ARG_PROFILE}
        INCLUDE_PATHS ${ARG_INCLUDE_PATHS}
        MACROS ${ARG_MACROS}
    )

    # Make command: compile to DXIL
    _shader_make_compile_dxil_command(
        OUT_COMMAND _dxil_compile_command
        SOURCE "${_source_abs_path}"
        DESTINATION "${_out_dxil_path}"
        ENTRYPOINT ${ARG_ENTRYPOINT}
        PROFILE ${ARG_PROFILE}
        INCLUDE_PATHS ${ARG_INCLUDE_PATHS}
        MACROS ${ARG_MACROS}
        ${_fwd_INCLUDE_REFLECTION}
    )
    if (_dxil_compile_command)
        list(APPEND _compile_commands ${_dxil_compile_command})
        list(APPEND _outputs "${_out_dxil_path}")
    endif ()

    # Make command: compile to SPIR-V
    _shader_make_compile_spirv_command(
        OUT_COMMAND _spirv_compile_command
        SOURCE "${_source_abs_path}"
        DESTINATION "${_out_spirv_path}"
        ENTRYPOINT ${ARG_ENTRYPOINT}
        PROFILE ${ARG_PROFILE}
        INCLUDE_PATHS ${ARG_INCLUDE_PATHS}
        MACROS ${ARG_MACROS}
        ${_fwd_INCLUDE_REFLECTION}
    )
    if (_spirv_compile_command)
        list(APPEND _compile_commands ${_spirv_compile_command})
        list(APPEND _outputs "${_out_spirv_path}")
    endif ()

    if (APPLE)
        set(_out_metal_path "${_out_shader_path}.metal")
        set(_out_air_path "${_out_shader_path}.air")
        set(_out_metallib_path "${_out_shader_path}.metallib")
        _shader_make_compile_metal_command(
            OUT_COMMAND _metal_compile_command
            SPIRV_SOURCE "${_out_spirv_path}"
            METAL_DESTINATION "${_out_metal_path}"
            AIR_DESTINATION "${_out_air_path}"
            METALLIB_DESTINATION "${_out_metallib_path}"
            ENTRYPOINT ${ARG_ENTRYPOINT}
            PROFILE ${ARG_PROFILE}
        )
        if (_metal_compile_command)
            list(APPEND _compile_commands ${_metal_compile_command})
            list(APPEND _outputs "${_out_metallib_path}")
        endif ()
    endif ()


    # Add custom commands
    if (CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        # Make use of DEPFILE to automatically rebuild dependency graphs when
        # includes are changed in HLSL source files.

        # Compile shader
        add_custom_command(
            OUTPUT ${_outputs}

            # Generate dependency file
            ${_depfile_command}

            # Compile shader
            ${_compile_commands}

            DEPFILE "${_dep_file}"
            COMMENT "Compiling ${_source_abs_path} to ${_outputs}"
        )
    else()
        # Fallback for generators without DEPFILE support
        message(WARNING
            "compile_hlsl_shader: This generator does not support depfiles. "
            "Shader include dependency changes (adding/removing #include directives) "
            "will not be detected automatically. You must reconfigure the project "
            "after modifying include relationships."
        )

        # Make sure the dir for the dep file exists
        get_filename_component(_dep_dir "${_dep_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dep_dir}")

        # Generate dependency file
        execute_process(
            ${_depfile_command}
            RESULT_VARIABLE _result
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if (NOT _result EQUAL 0)
            message(FATAL_ERROR "DXC exited with error code ${_result} when generating dependencies for ${_source_abs_path}")
        endif ()

        # Parse dependency file
        file(READ "${_dep_file}" _depfile_contents)
        string(REGEX REPLACE "^.*:[ \t]+" "" _deps_raw "${_depfile_contents}")
        string(REGEX REPLACE "\\\\[\r\n]+" "" _deps_raw "${_deps_raw}")
        string(REGEX REPLACE "[ \t\r\n]+" ";" _deps_raw "${_deps_raw}")
        list(FILTER _deps_raw EXCLUDE REGEX "^$")

        # Normalize paths
        set(_deps "")
        foreach(_dep ${_deps_raw})
            get_filename_component(_abs_dep "${_dep}" ABSOLUTE)
            list(APPEND _deps "${_abs_dep}")
        endforeach()

        # Compile shader
        add_custom_command(
            OUTPUT ${_outputs}
            ${_compile_commands}
            DEPENDS ${_deps}
            COMMENT "Compiling ${_source_abs_path} to ${_outputs}"
        )
    endif()

    set(${ARG_OUT_PATHS} "${_outputs}" PARENT_SCOPE)
endfunction()
