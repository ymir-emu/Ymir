#pragma once

/**
@file
@brief Includes all graphics context specifications present on the current platform.
*/

#if YMIR_PLATFORM_HAS_DIRECT3D
    // #include "gfx_context_spec_d3d11.hpp"
    #include "gfx_context_spec_d3d12.hpp"
#endif

#if YMIR_PLATFORM_HAS_VULKAN
    #include "gfx_context_spec_vulkan.hpp"
#endif

#if YMIR_PLATFORM_HAS_METAL
    #include "gfx_context_spec_metal.hpp"
#endif
