## Create the library target
add_library(ymir-core
    include/ymir/ymir.hpp
    include/ymir/version.hpp

    include/ymir/core/configuration.hpp
    include/ymir/core/configuration_defs.hpp
    include/ymir/core/hash.hpp
    include/ymir/core/scheduler.hpp
    include/ymir/core/scheduler_defs.hpp
    include/ymir/core/types.hpp

    include/ymir/db/cdb_rom_db.hpp
    include/ymir/db/game_db.hpp
    include/ymir/db/ipl_db.hpp
    include/ymir/db/rom_cart_db.hpp

    include/ymir/debug/cdblock_tracer_base.hpp
    include/ymir/debug/cd_drive_tracer_base.hpp
    include/ymir/debug/debug_break.hpp
    include/ymir/debug/scsp_tracer_base.hpp
    include/ymir/debug/scu_tracer_base.hpp
    include/ymir/debug/sh2_debug_defs.hpp
    include/ymir/debug/sh2_tracer_base.hpp
    include/ymir/debug/watchpoint_defs.hpp
    include/ymir/debug/ygr_tracer_base.hpp

    include/ymir/gpu/shaders/gpu_shaders.hpp
    include/ymir/gpu/shaders/gpu_shader_types.hpp

    include/ymir/hw/hw_defs.hpp

    include/ymir/hw/cart/cart_base.hpp
    include/ymir/hw/cart/cart_impl_bup.hpp
    include/ymir/hw/cart/cart_impl_dram.hpp
    include/ymir/hw/cart/cart_impl_none.hpp
    include/ymir/hw/cart/cart_impl_rom.hpp
    include/ymir/hw/cart/cart_slot.hpp

    include/ymir/hw/cdblock/cdblock.hpp
    include/ymir/hw/cdblock/cdblock_buffer.hpp
    include/ymir/hw/cdblock/cdblock_defs.hpp
    include/ymir/hw/cdblock/cdblock_filter.hpp
    include/ymir/hw/cdblock/cdblock_internal_callbacks.hpp
    include/ymir/hw/cdblock/cd_drive.hpp
    include/ymir/hw/cdblock/cd_drive_internal_callbacks.hpp
    include/ymir/hw/cdblock/ygr.hpp
    include/ymir/hw/cdblock/ygr_internal_callbacks.hpp

    include/ymir/hw/m68k/m68k.hpp
    include/ymir/hw/m68k/m68k_decode.hpp
    include/ymir/hw/m68k/m68k_defs.hpp
    include/ymir/hw/m68k/m68k_disasm.hpp

    include/ymir/hw/scsp/scsp.hpp
    include/ymir/hw/scsp/scsp_callbacks.hpp
    include/ymir/hw/scsp/scsp_defs.hpp
    include/ymir/hw/scsp/scsp_midi_defs.hpp
    include/ymir/hw/scsp/scsp_dsp.hpp
    include/ymir/hw/scsp/scsp_dsp_instr.hpp
    include/ymir/hw/scsp/scsp_internal_callbacks.hpp
    include/ymir/hw/scsp/scsp_slot.hpp
    include/ymir/hw/scsp/scsp_timer.hpp

    include/ymir/hw/scu/scu.hpp
    include/ymir/hw/scu/scu_callbacks.hpp
    include/ymir/hw/scu/scu_defs.hpp
    include/ymir/hw/scu/scu_dma.hpp
    include/ymir/hw/scu/scu_dsp.hpp
    include/ymir/hw/scu/scu_dsp_instr.hpp
    include/ymir/hw/scu/scu_dsp_disasm.hpp
    include/ymir/hw/scu/scu_internal_callbacks.hpp

    include/ymir/hw/sh1/sh1.hpp
    include/ymir/hw/sh1/sh1_ad.hpp
    include/ymir/hw/sh1/sh1_bsc.hpp
    include/ymir/hw/sh1/sh1_decode.hpp
    include/ymir/hw/sh1/sh1_defs.hpp
    #include/ymir/hw/sh1/sh1_disasm.hpp
    include/ymir/hw/sh1/sh1_dmac.hpp
    include/ymir/hw/sh1/sh1_excpt.hpp
    include/ymir/hw/sh1/sh1_intc.hpp
    include/ymir/hw/sh1/sh1_internal_callbacks.hpp
    include/ymir/hw/sh1/sh1_itu.hpp
    include/ymir/hw/sh1/sh1_pfc.hpp
    include/ymir/hw/sh1/sh1_power.hpp
    include/ymir/hw/sh1/sh1_regs.hpp
    include/ymir/hw/sh1/sh1_sci.hpp
    include/ymir/hw/sh1/sh1_tpc.hpp
    include/ymir/hw/sh1/sh1_wdt.hpp

    include/ymir/hw/sh2/sh2.hpp
    include/ymir/hw/sh2/sh2_bsc.hpp
    include/ymir/hw/sh2/sh2_cache.hpp
    include/ymir/hw/sh2/sh2_decode.hpp
    include/ymir/hw/sh2/sh2_disasm.hpp
    include/ymir/hw/sh2/sh2_divu.hpp
    include/ymir/hw/sh2/sh2_dmac.hpp
    include/ymir/hw/sh2/sh2_excpt.hpp
    include/ymir/hw/sh2/sh2_frt.hpp
    include/ymir/hw/sh2/sh2_intc.hpp
    include/ymir/hw/sh2/sh2_internal_callbacks.hpp
    include/ymir/hw/sh2/sh2_power.hpp
    include/ymir/hw/sh2/sh2_regs.hpp
    include/ymir/hw/sh2/sh2_sci.hpp
    include/ymir/hw/sh2/sh2_ubc.hpp
    include/ymir/hw/sh2/sh2_wdt.hpp

    include/ymir/hw/smpc/rtc.hpp
    include/ymir/hw/smpc/smpc.hpp
    include/ymir/hw/smpc/smpc_defs.hpp
    include/ymir/hw/smpc/smpc_internal_callbacks.hpp

    include/ymir/hw/smpc/peripheral/peripheral_base.hpp
    include/ymir/hw/smpc/peripheral/peripheral_callbacks.hpp
    include/ymir/hw/smpc/peripheral/peripheral_defs.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_control_pad.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_shuttle_mouse.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_virtua_gun.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_null.hpp
    include/ymir/hw/smpc/peripheral/peripheral_port.hpp
    include/ymir/hw/smpc/peripheral/peripheral_report.hpp
    include/ymir/hw/smpc/peripheral/peripheral_state_common.hpp

    include/ymir/hw/vdp/vdp.hpp
    include/ymir/hw/vdp/vdp_callbacks.hpp
    include/ymir/hw/vdp/vdp_common_defs.hpp
    include/ymir/hw/vdp/vdp_configs.hpp
    include/ymir/hw/vdp/vdp_defs.hpp
    include/ymir/hw/vdp/vdp_devlog.hpp
    include/ymir/hw/vdp/vdp_internal_callbacks.hpp
    include/ymir/hw/vdp/vdp_state.hpp
    include/ymir/hw/vdp/vdp1_defs.hpp
    include/ymir/hw/vdp/vdp1_regs.hpp
    include/ymir/hw/vdp/vdp2_defs.hpp
    include/ymir/hw/vdp/vdp2_regs.hpp

    include/ymir/hw/vdp/renderer/vdp_renderer.hpp
    include/ymir/hw/vdp/renderer/vdp_renderer_base.hpp
    include/ymir/hw/vdp/renderer/vdp_renderer_defs.hpp
    include/ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp
    include/ymir/hw/vdp/renderer/vdp_renderer_null.hpp
    include/ymir/hw/vdp/renderer/vdp_renderer_sw.hpp

    include/ymir/hw/vdp/renderer/common/vdp1_steppers.hpp

    include/ymir/media/cd_defs.hpp
    include/ymir/media/cd_interface.hpp
    include/ymir/media/cd_utils.hpp
    include/ymir/media/disc.hpp
    include/ymir/media/filesystem.hpp
    include/ymir/media/frame_address.hpp
    include/ymir/media/host_cd.hpp
    include/ymir/media/iso9660.hpp
    include/ymir/media/media_defs.hpp
    include/ymir/media/saturn_header.hpp
    include/ymir/media/scsi.hpp
    include/ymir/media/subheader.hpp

    include/ymir/media/cd_device/cd_device_base.hpp
    include/ymir/media/cd_device/cd_device_null.hpp
    include/ymir/media/cd_device/cd_device_host.hpp
    include/ymir/media/cd_device/cd_device_image.hpp

    include/ymir/media/loader/loader.hpp
    include/ymir/media/loader/loader_bin_cue.hpp
    include/ymir/media/loader/loader_chd.hpp
    include/ymir/media/loader/loader_img_ccd_sub.hpp
    include/ymir/media/loader/loader_iso.hpp
    include/ymir/media/loader/loader_mdf_mds.hpp
    include/ymir/media/loader/loader_result.hpp

    include/ymir/media/binary_reader/binary_reader.hpp
    include/ymir/media/binary_reader/binary_reader_composite.hpp
    include/ymir/media/binary_reader/binary_reader_file.hpp
    include/ymir/media/binary_reader/binary_reader_impl.hpp
    include/ymir/media/binary_reader/binary_reader_mem.hpp
    include/ymir/media/binary_reader/binary_reader_mmap.hpp
    include/ymir/media/binary_reader/binary_reader_subview.hpp
    include/ymir/media/binary_reader/binary_reader_zero.hpp

    include/ymir/savestate/savestate.hpp
    include/ymir/savestate/savestate_cdblock.hpp
    include/ymir/savestate/savestate_cd_drive.hpp
    include/ymir/savestate/savestate_cd_interface.hpp
    include/ymir/savestate/savestate_m68k.hpp
    include/ymir/savestate/savestate_scheduler.hpp
    include/ymir/savestate/savestate_scsp.hpp
    include/ymir/savestate/savestate_scsp_dsp.hpp
    include/ymir/savestate/savestate_scsp_slot.hpp
    include/ymir/savestate/savestate_scsp_timer.hpp
    include/ymir/savestate/savestate_scu.hpp
    include/ymir/savestate/savestate_sh1.hpp
    include/ymir/savestate/savestate_sh2.hpp
    include/ymir/savestate/savestate_smpc.hpp
    include/ymir/savestate/savestate_system.hpp
    include/ymir/savestate/savestate_vdp.hpp
    include/ymir/savestate/savestate_ygr.hpp

    include/ymir/sys/backup_ram.hpp
    include/ymir/sys/backup_ram_defs.hpp
    include/ymir/sys/bus.hpp
    include/ymir/sys/clocks.hpp
    include/ymir/sys/memory.hpp
    include/ymir/sys/memory_defs.hpp
    include/ymir/sys/saturn.hpp
    include/ymir/sys/system.hpp
    include/ymir/sys/system_internal_callbacks.hpp

    include/ymir/util/arith_ops.hpp
    include/ymir/util/backup_datetime.hpp
    include/ymir/util/bit_ops.hpp
    include/ymir/util/bitmask_enum.hpp
    include/ymir/util/callback.hpp
    include/ymir/util/compiler_info.hpp
    include/ymir/util/constexpr_for.hpp
    include/ymir/util/data_ops.hpp
    include/ymir/util/date_time.hpp
    include/ymir/util/dev_assert.hpp
    include/ymir/util/dev_log.hpp
    include/ymir/util/dirty_bitmap.hpp
    include/ymir/util/event.hpp
    include/ymir/util/function_info.hpp
    include/ymir/util/hashing.hpp
    include/ymir/util/inline.hpp
    include/ymir/util/lsn_denormals.hpp
    include/ymir/util/observable.hpp
    include/ymir/util/process.hpp
    include/ymir/util/ratio.hpp
    include/ymir/util/result.hpp
    include/ymir/util/scope_guard.hpp
    include/ymir/util/size_ops.hpp
    include/ymir/util/string.hpp
    include/ymir/util/thread_name.hpp
    include/ymir/util/type_traits_ex.hpp
    include/ymir/util/unreachable.hpp
    include/ymir/util/virtual_memory.hpp


    src/ymir/ymir.cpp

    src/ymir/core/configuration.cpp
    src/ymir/core/hash.cpp

    src/ymir/db/cdb_rom_db.cpp
    src/ymir/db/game_db.cpp
    src/ymir/db/ipl_db.cpp
    src/ymir/db/rom_cart_db.cpp

    src/ymir/hw/cart/cart_impl_bup.cpp
    src/ymir/hw/cart/cart_slot.cpp

    src/ymir/hw/cdblock/cdblock.cpp
    src/ymir/hw/cdblock/cdblock_devlog.hpp
    src/ymir/hw/cdblock/cdblock_partition_manager.cpp
    src/ymir/hw/cdblock/cd_drive.cpp
    src/ymir/hw/cdblock/ygr.cpp

    src/ymir/hw/m68k/m68k.cpp
    src/ymir/hw/m68k/m68k_addr_modes.hpp
    src/ymir/hw/m68k/m68k_decode.cpp
    src/ymir/hw/m68k/m68k_disasm.cpp

    src/ymir/hw/scsp/scsp.cpp
    src/ymir/hw/scsp/scsp_dsp.cpp
    src/ymir/hw/scsp/scsp_slot.cpp

    src/ymir/hw/scu/scu.cpp
    src/ymir/hw/scu/scu_devlog.hpp
    src/ymir/hw/scu/scu_dsp.cpp
    src/ymir/hw/scu/scu_dsp_disasm.cpp

    src/ymir/hw/smpc/rtc.cpp
    src/ymir/hw/smpc/smpc.cpp

    src/ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_control_pad.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_shuttle_mouse.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_virtua_gun.cpp

    src/ymir/hw/sh1/sh1.cpp
    src/ymir/hw/sh1/sh1_decode.cpp
    #src/ymir/hw/sh1/sh1_disasm.cpp

    src/ymir/hw/sh2/sh2.cpp
    src/ymir/hw/sh2/sh2_decode.cpp
    src/ymir/hw/sh2/sh2_disasm.cpp

    src/ymir/hw/vdp/vdp.cpp

    src/ymir/hw/vdp/renderer/vdp_renderer_sw.cpp

    src/ymir/media/cd_defs.cpp
    src/ymir/media/cd_interface.cpp
    src/ymir/media/cd_utils.cpp
    src/ymir/media/filesystem.cpp
    src/ymir/media/media_defs.cpp
    src/ymir/media/saturn_header.cpp

    src/ymir/media/cd_device/cd_device_base.cpp
    src/ymir/media/cd_device/cd_device_host.cpp
    src/ymir/media/cd_device/cd_device_image.cpp

    src/ymir/media/loader/loader.cpp
    src/ymir/media/loader/loader_bin_cue.cpp
    src/ymir/media/loader/loader_chd.cpp
    src/ymir/media/loader/loader_img_ccd_sub.cpp
    src/ymir/media/loader/loader_iso.cpp
    src/ymir/media/loader/loader_mdf_mds.cpp

    src/ymir/sys/backup_ram.cpp
    src/ymir/sys/memory.cpp
    src/ymir/sys/null_program.hpp
    src/ymir/sys/saturn.cpp

    src/ymir/util/backup_datetime.cpp
    src/ymir/util/date_time.cpp
    src/ymir/util/event.cpp
    src/ymir/util/process.cpp
    src/ymir/util/string.cpp
    src/ymir/util/thread_name.cpp
    src/ymir/util/virtual_memory.cpp
)

add_library(ymir::ymir-core ALIAS ymir-core)
set_target_properties(ymir-core PROPERTIES
                      VERSION ${Ymir_core_VERSION}
                      SOVERSION ${Ymir_core_VERSION_MAJOR})
target_include_directories(ymir-core
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(ymir-core PUBLIC cxx_std_20)

## Add OS-specific source files for host CD drive support
if (Ymir_FF_HOST_CD_DRIVES)
    if (WIN32)
        target_sources(ymir-core PRIVATE src/ymir/media/host_cd_windows.cpp)
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(ymir-core PRIVATE src/ymir/media/host_cd_linux.cpp)
    elseif (APPLE)
        target_sources(ymir-core PRIVATE src/ymir/media/host_cd_macos.cpp)
    else ()
        target_sources(ymir-core PRIVATE src/ymir/media/host_cd_dummy.cpp)
    endif ()
else ()
    target_sources(ymir-core PRIVATE src/ymir/media/host_cd_dummy.cpp)
endif ()

## Add OS-specific source files for graphics
if (WIN32)
    target_sources(ymir-core
        PUBLIC
            include/ymir/gpu/d3d12/d3d12_commands.hpp
            include/ymir/gpu/d3d12/d3d12_debug.hpp
            include/ymir/gpu/d3d12/d3d12_descriptor_heap.hpp
            include/ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp
            include/ymir/gpu/d3d12/d3d12_device.hpp
            include/ymir/gpu/d3d12/d3d12_fence.hpp
            include/ymir/gpu/d3d12/d3d12_object_wrapper.hpp
            include/ymir/gpu/d3d12/d3d12_pipeline_state.hpp
            include/ymir/gpu/d3d12/d3d12_resource.hpp
            include/ymir/gpu/d3d12/d3d12_root_signature.hpp
            include/ymir/gpu/d3d12/d3d12_swap_chain.hpp

            include/ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.hpp

        PRIVATE
            src/ymir/gpu/d3d12/d3d12_debug.cpp

            src/ymir/gpu/shaders/gpu_shaders_windows.cpp

            src/ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.cpp
    )
elseif (APPLE)
    target_sources(ymir-core PRIVATE
        src/ymir/gpu/shaders/gpu_shaders_macos.cpp
    )
elseif (UNIX) # Linux and FreeBSD
    target_sources(ymir-core PRIVATE
        src/ymir/gpu/shaders/gpu_shaders_unixlike.cpp
    )
else ()
    target_sources(ymir-core PRIVATE
        src/ymir/gpu/shaders/gpu_shaders_other.cpp
    )
endif ()

if (Vulkan_FOUND)
    target_sources(ymir-core
        PUBLIC
            include/ymir/gpu/vulkan/vulkan_api.hpp
            include/ymir/gpu/vulkan/vulkan_debug.hpp
            include/ymir/gpu/vulkan/vulkan_descriptor_heap.hpp
            include/ymir/gpu/vulkan/vulkan_descriptor_update_batch.hpp
            include/ymir/gpu/vulkan/vulkan_memory.hpp
            include/ymir/gpu/vulkan/vulkan_swap_chain.hpp
            include/ymir/gpu/vulkan/vulkan_synchronization.hpp
			
			PRIVATE
            src/ymir/gpu/vulkan/vulkan_api.cpp
            src/ymir/gpu/vulkan/vulkan_debug.cpp
            src/ymir/gpu/vulkan/vulkan_descriptor_heap.cpp
			src/ymir/gpu/vulkan/vulkan_descriptor_update_batch.cpp
            src/ymir/gpu/vulkan/vulkan_memory.cpp
            src/ymir/gpu/vulkan/vulkan_swap_chain.cpp
            src/ymir/gpu/vulkan/vulkan_synchronization.cpp
    )
endif ()

## Compile VDP1 and VDP2 renderer shaders
set(SHADER_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(COMPUTE_SHADER_SOURCES
    res/shaders/src/vdp/cs_vdp2_compose.hlsl
    res/shaders/src/vdp/cs_vdp2_render_bgs.hlsl
    res/shaders/src/vdp/cs_vdp2_render_sprite.hlsl
)
## TODO: add per-platform shader code if needed
# if (WIN32)
#     list(APPEND COMPUTE_SHADER_SOURCES
#         res/shaders/vdp/d3d12/some_shader_cs.hlsl
#     )
# elseif (APPLE)
# elseif (UNIX) # Linux and FreeBSD
# endif ()

set(COMPILED_SHADERS "")
foreach (SHADER_SRC ${COMPUTE_SHADER_SOURCES})
    compile_shader(
        OUT_PATHS SHADER_BINARY_FILES
        SOURCE "${SHADER_SRC}"
        WHENCE "${SHADER_INPUT_DIR}"
        DESTINATION "${SHADER_OUTPUT_DIR}"
        ENTRYPOINT "CSMain"
        PROFILE "cs_6_0"
        INCLUDE_PATHS "${SHADER_INPUT_DIR}/include"
    )
    list(APPEND COMPILED_SHADERS ${SHADER_BINARY_FILES})
endforeach ()

# Configure properties for shader source files in Visual Studio solutions
if (WIN32)
    set_source_files_properties(
        ${COMPUTE_SHADER_SOURCES}
        PROPERTIES
            VS_SHADER_TYPE "Compute"
            VS_SHADER_MODEL "6.0"
            VS_SHADER_ENTRYPOINT "CSMain"
    )
endif ()


## Create the embedded shaders target
MESSAGE(STATUS "Generating core shader resource library: ${COMPILED_SHADERS}")
cmrc_add_resource_library(ymir-core-shaders
    ALIAS ymir-core::shaders
    NAMESPACE ymir_core_shaders
    WHENCE "${SHADER_OUTPUT_DIR}"
    ${COMPILED_SHADERS}
)


## Add dependencies
target_link_libraries(ymir-core PUBLIC
    ymir-core::shaders

    fmt::fmt
    mio
    concurrentqueue
    xxHash::xxHash
    chdr-static
    stb::stb
    dr_libs::dr_libs
)
if (WIN32)
    target_link_libraries(ymir-core PUBLIC
        synchronization  # for WaitOnAddress and WakeByAddressAll

        # Direct3D 11 and 12
        d3d12
        dxgi
        dxguid
        dxcompiler

        WIL::WIL
    )
elseif (APPLE)
    find_package(Iconv REQUIRED)
    target_link_libraries(ymir-core PUBLIC
        Iconv::Iconv
    )
elseif (UNIX) # Linux and FreeBSD
    find_package(Iconv REQUIRED)
    target_link_libraries(ymir-core PUBLIC
        Iconv::Iconv
    )
    if (CMAKE_SYSTEM_NAME MATCHES FreeBSD)
        # Prevent third-party package libiconv from interfering with iconv from libc.
        target_compile_definitions(ymir-core PUBLIC "LIBICONV_PLUG")
    endif ()
endif ()

if (Vulkan_FOUND)
    message(STATUS "Adding Vulkan to ymir-core")
    target_link_libraries(ymir-core PRIVATE
        Vulkan::Vulkan
        Vulkan::SPIRV-Tools
    )
endif ()


# Build precompiled header
target_precompile_headers(ymir-core
    PUBLIC
        "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<array$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<atomic$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<bit$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cassert$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<charconv$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cinttypes$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<climits$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cmath$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<concepts$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<condition_variable$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cstddef$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cstdint$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<cstdlib$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<deque$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<filesystem$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fstream$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<future$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<initializer_list$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<iosfwd$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<iostream$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<iterator$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<limits$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<memory$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<mutex$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<new$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<numbers$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<numeric$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<optional$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<queue$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<random$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<set$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<span$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<sstream$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<thread$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<type_traits$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<unordered_map$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<unordered_set$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<utility$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<variant$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"

        "$<$<COMPILE_LANGUAGE:CXX>:<fmt/format.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fmt/std.h$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<fmt/xchar.h$<ANGLE-R>>"

        "$<$<COMPILE_LANGUAGE:CXX>:<mio/mmap.hpp$<ANGLE-R>>"
)

## Define version macros
if (DEFINED Ymir_VERSION_PRERELEASE AND NOT Ymir_VERSION_PRERELEASE STREQUAL "")
    set(Ymir_VERSION "${Ymir_VERSION}-${Ymir_VERSION_PRERELEASE}")
endif ()
if (Ymir_DEV_BUILD)
    set(Ymir_VERSION "${Ymir_VERSION}-dev")
endif ()
if (DEFINED Ymir_VERSION_BUILD AND NOT Ymir_VERSION_BUILD STREQUAL "")
    set(Ymir_VERSION "${Ymir_VERSION}+${Ymir_VERSION_BUILD}")
endif ()
message(STATUS "Ymir version string: ${Ymir_VERSION}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION="${Ymir_VERSION}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_MAJOR=${Ymir_VERSION_MAJOR})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_MINOR=${Ymir_VERSION_MINOR})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_PATCH=${Ymir_VERSION_PATCH})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_PRERELEASE="${Ymir_VERSION_PRERELEASE}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_BUILD="${Ymir_VERSION_BUILD}")
if (DEFINED Ymir_BUILD_TIMESTAMP AND NOT Ymir_BUILD_TIMESTAMP STREQUAL "")
    target_compile_definitions(ymir-core PUBLIC Ymir_BUILD_TIMESTAMP="${Ymir_BUILD_TIMESTAMP}")
endif ()

## Define additional macros
target_compile_definitions(ymir-core PUBLIC "Ymir_ENABLE_DEVLOG=$<BOOL:${Ymir_ENABLE_DEVLOG}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_DEV_ASSERTIONS=$<BOOL:${Ymir_ENABLE_DEV_ASSERTIONS}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_DEV_BUILD=$<BOOL:${Ymir_DEV_BUILD}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_EXTRA_INLINING=$<BOOL:${Ymir_EXTRA_INLINING}>")
target_compile_definitions(ymir-core PUBLIC "YMIR_PLATFORM_HAS_DIRECT3D=$<BOOL:${WIN32}>")
target_compile_definitions(ymir-core PUBLIC "YMIR_PLATFORM_HAS_VULKAN=$<BOOL:${Vulkan_FOUND}>")
target_compile_definitions(ymir-core PUBLIC "YMIR_PLATFORM_HAS_METAL=$<BOOL:${APPLE}>")
target_compile_definitions(ymir-core PUBLIC "TOML_EXCEPTIONS=0")

## Define feature flags macros
## Template:
## target_compile_definitions(ymir-core PUBLIC "Ymir_FF_[NAME]=$<BOOL:${Ymir_FF_[NAME]}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_FF_HOST_CD_DRIVES=$<BOOL:${Ymir_FF_HOST_CD_DRIVES}>")

## Generate the export header and attach it to the target
include(GenerateExportHeader)
generate_export_header(ymir-core EXPORT_FILE_NAME include/ymir/export.h)
target_compile_definitions(ymir-core PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:Ymir_core_STATIC_DEFINE>")
target_include_directories(ymir-core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-core")
    set_property(TARGET ymir-core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    target_compile_definitions(ymir-core PUBLIC Ymir_AVX2=1)
    if (MSVC)
        target_compile_options(ymir-core PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-core PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Enable unity build
#set_target_properties(ymir-core PROPERTIES UNITY_BUILD ON)
#set_target_properties(ymir-core PROPERTIES UNITY_BUILD_BATCH_SIZE 0)

## Configure Visual Studio filters
if (MSVC)
    vs_set_filters(TARGET ymir-core)
    set_target_properties(ymir-core PROPERTIES FOLDER "Ymir")
endif ()
