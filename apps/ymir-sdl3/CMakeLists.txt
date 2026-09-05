## Create the executable target
add_executable(ymir-sdl3
    src/main.cpp
    src/winmain.cpp

    src/app/app.cpp
    src/app/app.hpp
    src/app/audio_system.cpp
    src/app/audio_system.hpp
    src/app/cmdline_opts.hpp
    src/app/display.hpp
    src/app/message.hpp
    src/app/profile.cpp
    src/app/profile.hpp
    src/app/rewind_buffer.cpp
    src/app/rewind_buffer.hpp
    src/app/rom_manager.cpp
    src/app/rom_manager.hpp
    src/app/settings.cpp
    src/app/settings.hpp
    src/app/settings_defaults.hpp
    src/app/shared_context.cpp
    src/app/shared_context.hpp
    src/app/toml_implementation.cpp
    src/app/update_checker.cpp
    src/app/update_checker.hpp

    src/app/debug/cdblock_tracer.cpp
    src/app/debug/cdblock_tracer.hpp
    src/app/debug/cd_drive_tracer.cpp
    src/app/debug/cd_drive_tracer.hpp
    src/app/debug/scsp_tracer.cpp
    src/app/debug/scsp_tracer.hpp
    src/app/debug/scu_tracer.cpp
    src/app/debug/scu_tracer.hpp
    src/app/debug/sh2_exec_analyst.cpp
    src/app/debug/sh2_exec_analyst.hpp
    src/app/debug/sh2_tracer.cpp
    src/app/debug/sh2_tracer.hpp
    src/app/debug/ygr_tracer.cpp
    src/app/debug/ygr_tracer.hpp

    src/app/events/emu_debug_event_factory.cpp
    src/app/events/emu_debug_event_factory.hpp
    src/app/events/emu_event.hpp
    src/app/events/emu_event_factory.cpp
    src/app/events/emu_event_factory.hpp
    src/app/events/gui_event.hpp
    src/app/events/gui_event_factory.hpp

    src/app/input/input_action.hpp
    src/app/input/input_backend_sdl3.cpp
    src/app/input/input_backend_sdl3.hpp
    src/app/input/input_bind.hpp
    src/app/input/input_context.cpp
    src/app/input/input_context.hpp
    src/app/input/input_events.cpp
    src/app/input/input_events.hpp
    src/app/input/input_primitives.cpp
    src/app/input/input_primitives.hpp
    src/app/input/input_utils.hpp

    src/app/services/graphics_service.cpp
    src/app/services/graphics_service.hpp
    src/app/services/midi_service.cpp
    src/app/services/midi_service.hpp
    src/app/services/midi_types.hpp
    src/app/services/save_state_service.cpp
    src/app/services/save_state_service.hpp
    src/app/services/save_state_types.hpp
    src/app/services/screenshot_service.cpp
    src/app/services/screenshot_service.hpp
    src/app/services/screenshot_types.hpp
    src/app/services/update_checker_service.cpp
    src/app/services/update_checker_service.hpp
    src/app/services/mouse_capture_service.cpp
    src/app/services/mouse_capture_service.hpp
    src/app/services/rom_service.cpp
    src/app/services/rom_service.hpp
    src/app/services/disc_service.cpp
    src/app/services/disc_service.hpp
    src/app/services/discord_rpc_service.cpp
    src/app/services/discord_rpc_service.hpp
    src/app/services/display_service.cpp
    src/app/services/display_service.hpp
    src/app/services/file_dialog_service.cpp
    src/app/services/file_dialog_service.hpp
    src/app/services/input_service.cpp
    src/app/services/input_service.hpp
    src/app/services/persistence_service.cpp
    src/app/services/persistence_service.hpp
    src/app/services/window_manager_service.cpp
    src/app/services/window_manager_service.hpp

    src/app/services/gfx/gfx_adapters.cpp
    src/app/services/gfx/gfx_adapters.hpp
    src/app/services/gfx/gfx_context.cpp
    src/app/services/gfx/gfx_context.hpp
    src/app/services/gfx/gfx_context_impl_null.hpp
    src/app/services/gfx/gfx_context_impl_sdl_renderer.cpp
    src/app/services/gfx/gfx_context_impl_sdl_renderer.hpp
    src/app/services/gfx/gfx_context_impls.hpp
    src/app/services/gfx/gfx_context_specs.hpp
    src/app/services/gfx/gfx_gui_types.hpp
    src/app/services/gfx/gfx_types.cpp
    src/app/services/gfx/gfx_types.hpp

    src/app/ui/window_base.cpp
    src/app/ui/window_base.hpp

    src/app/ui/fonts/IconsMaterialSymbols.h

    src/app/ui/model/debug/sh2_breakpoints_manager.cpp
    src/app/ui/model/debug/sh2_breakpoints_manager.hpp
    src/app/ui/model/debug/sh2_debugger_model.cpp
    src/app/ui/model/debug/sh2_debugger_model.hpp
    src/app/ui/model/debug/sh2_watchpoints_manager.cpp
    src/app/ui/model/debug/sh2_watchpoints_manager.hpp

    src/app/ui/state/debug/memory_viewer_state.hpp

    src/app/ui/utils/debug/vdp_window_set_printer.hpp

    src/app/ui/views/backup_memory_view.cpp
    src/app/ui/views/backup_memory_view.hpp

    src/app/ui/views/debug/cdblock_cmd_trace_view.cpp
    src/app/ui/views/debug/cdblock_cmd_trace_view.hpp
    src/app/ui/views/debug/cdblock_drive_state_trace_view.cpp
    src/app/ui/views/debug/cdblock_drive_state_trace_view.hpp
    src/app/ui/views/debug/cdblock_filters_view.cpp
    src/app/ui/views/debug/cdblock_filters_view.hpp
    src/app/ui/views/debug/cdblock_partitions_view.cpp
    src/app/ui/views/debug/cdblock_partitions_view.hpp
    src/app/ui/views/debug/cdblock_ygr_cmd_trace_view.cpp
    src/app/ui/views/debug/cdblock_ygr_cmd_trace_view.hpp
    src/app/ui/views/debug/debug_output_view.cpp
    src/app/ui/views/debug/debug_output_view.hpp
    src/app/ui/views/debug/scsp_kyonex_trace_view.cpp
    src/app/ui/views/debug/scsp_kyonex_trace_view.hpp
    src/app/ui/views/debug/scsp_output_view.cpp
    src/app/ui/views/debug/scsp_output_view.hpp
    src/app/ui/views/debug/scsp_slots_view.cpp
    src/app/ui/views/debug/scsp_slots_view.hpp
    src/app/ui/views/debug/scu_dma_registers_view.cpp
    src/app/ui/views/debug/scu_dma_registers_view.hpp
    src/app/ui/views/debug/scu_dma_state_view.cpp
    src/app/ui/views/debug/scu_dma_state_view.hpp
    src/app/ui/views/debug/scu_dma_trace_view.cpp
    src/app/ui/views/debug/scu_dma_trace_view.hpp
    src/app/ui/views/debug/scu_dsp_data_ram_view.cpp
    src/app/ui/views/debug/scu_dsp_data_ram_view.hpp
    src/app/ui/views/debug/scu_dsp_disassembly_view.cpp
    src/app/ui/views/debug/scu_dsp_disassembly_view.hpp
    src/app/ui/views/debug/scu_dsp_dma_registers_view.cpp
    src/app/ui/views/debug/scu_dsp_dma_registers_view.hpp
    src/app/ui/views/debug/scu_dsp_dma_trace_view.cpp
    src/app/ui/views/debug/scu_dsp_dma_trace_view.hpp
    src/app/ui/views/debug/scu_dsp_registers_view.cpp
    src/app/ui/views/debug/scu_dsp_registers_view.hpp
    src/app/ui/views/debug/scu_interrupt_trace_view.cpp
    src/app/ui/views/debug/scu_interrupt_trace_view.hpp
    src/app/ui/views/debug/scu_interrupts_view.cpp
    src/app/ui/views/debug/scu_interrupts_view.hpp
    src/app/ui/views/debug/scu_registers_view.cpp
    src/app/ui/views/debug/scu_registers_view.hpp
    src/app/ui/views/debug/scu_timers_view.cpp
    src/app/ui/views/debug/scu_timers_view.hpp
    src/app/ui/views/debug/sh2_breakpoints_view.cpp
    src/app/ui/views/debug/sh2_breakpoints_view.hpp
    src/app/ui/views/debug/sh2_cache_entries_view.cpp
    src/app/ui/views/debug/sh2_cache_entries_view.hpp
    src/app/ui/views/debug/sh2_cache_register_view.cpp
    src/app/ui/views/debug/sh2_cache_register_view.hpp
    src/app/ui/views/debug/sh2_call_stack_view.cpp
    src/app/ui/views/debug/sh2_call_stack_view.hpp
    src/app/ui/views/debug/sh2_data_stack_view.cpp
    src/app/ui/views/debug/sh2_data_stack_view.hpp
    src/app/ui/views/debug/sh2_debug_toolbar_view.cpp
    src/app/ui/views/debug/sh2_debug_toolbar_view.hpp
    src/app/ui/views/debug/sh2_disasm_dump_view.cpp
    src/app/ui/views/debug/sh2_disasm_dump_view.hpp
    src/app/ui/views/debug/sh2_disassembly_view.cpp
    src/app/ui/views/debug/sh2_disassembly_view.hpp
    src/app/ui/views/debug/sh2_divu_registers_view.cpp
    src/app/ui/views/debug/sh2_divu_registers_view.hpp
    src/app/ui/views/debug/sh2_divu_statistics_view.cpp
    src/app/ui/views/debug/sh2_divu_statistics_view.hpp
    src/app/ui/views/debug/sh2_divu_trace_view.cpp
    src/app/ui/views/debug/sh2_divu_trace_view.hpp
    src/app/ui/views/debug/sh2_dmac_channel_view.cpp
    src/app/ui/views/debug/sh2_dmac_channel_view.hpp
    src/app/ui/views/debug/sh2_dmac_registers_view.cpp
    src/app/ui/views/debug/sh2_dmac_registers_view.hpp
    src/app/ui/views/debug/sh2_dmac_trace_view.cpp
    src/app/ui/views/debug/sh2_dmac_trace_view.hpp
    src/app/ui/views/debug/sh2_exception_vectors_view.cpp
    src/app/ui/views/debug/sh2_exception_vectors_view.hpp
    src/app/ui/views/debug/sh2_frt_view.cpp
    src/app/ui/views/debug/sh2_frt_view.hpp
    src/app/ui/views/debug/sh2_interrupt_trace_view.cpp
    src/app/ui/views/debug/sh2_interrupt_trace_view.hpp
    src/app/ui/views/debug/sh2_interrupts_view.cpp
    src/app/ui/views/debug/sh2_interrupts_view.hpp
    src/app/ui/views/debug/sh2_power_view.cpp
    src/app/ui/views/debug/sh2_power_view.hpp
    src/app/ui/views/debug/sh2_registers_view.cpp
    src/app/ui/views/debug/sh2_registers_view.hpp
    src/app/ui/views/debug/sh2_watchpoints_view.cpp
    src/app/ui/views/debug/sh2_watchpoints_view.hpp
    src/app/ui/views/debug/sh2_wdt_view.cpp
    src/app/ui/views/debug/sh2_wdt_view.hpp
    src/app/ui/views/debug/vdp1_registers_view.cpp
    src/app/ui/views/debug/vdp1_registers_view.hpp
    src/app/ui/views/debug/vdp2_bg_layer_params_view.cpp
    src/app/ui/views/debug/vdp2_bg_layer_params_view.hpp
    src/app/ui/views/debug/vdp2_color_calc_params_view.cpp
    src/app/ui/views/debug/vdp2_color_calc_params_view.hpp
    src/app/ui/views/debug/vdp2_cram_view.cpp
    src/app/ui/views/debug/vdp2_cram_view.hpp
    src/app/ui/views/debug/vdp2_debug_overlay_view.cpp
    src/app/ui/views/debug/vdp2_debug_overlay_view.hpp
    src/app/ui/views/debug/vdp2_layer_visibility_view.cpp
    src/app/ui/views/debug/vdp2_layer_visibility_view.hpp
    src/app/ui/views/debug/vdp2_registers_view.cpp
    src/app/ui/views/debug/vdp2_registers_view.hpp
    src/app/ui/views/debug/vdp2_sprite_layer_params_view.cpp
    src/app/ui/views/debug/vdp2_sprite_layer_params_view.hpp
    src/app/ui/views/debug/vdp2_vram_access_patterns_view.cpp
    src/app/ui/views/debug/vdp2_vram_access_patterns_view.hpp
    src/app/ui/views/debug/vdp2_window_params_view.cpp
    src/app/ui/views/debug/vdp2_window_params_view.hpp

    src/app/ui/views/settings/analog_pad_config_view.cpp
    src/app/ui/views/settings/analog_pad_config_view.hpp
    src/app/ui/views/settings/arcade_racer_config_view.cpp
    src/app/ui/views/settings/arcade_racer_config_view.hpp
    src/app/ui/views/settings/audio_settings_view.cpp
    src/app/ui/views/settings/audio_settings_view.hpp
    src/app/ui/views/settings/cartridge_settings_view.cpp
    src/app/ui/views/settings/cartridge_settings_view.hpp
    src/app/ui/views/settings/cdblock_settings_view.cpp
    src/app/ui/views/settings/cdblock_settings_view.hpp
    src/app/ui/views/settings/control_pad_config_view.cpp
    src/app/ui/views/settings/control_pad_config_view.hpp
    src/app/ui/views/settings/general_settings_view.cpp
    src/app/ui/views/settings/general_settings_view.hpp
    src/app/ui/views/settings/gui_settings_view.cpp
    src/app/ui/views/settings/gui_settings_view.hpp
    src/app/ui/views/settings/hotkeys_settings_view.cpp
    src/app/ui/views/settings/hotkeys_settings_view.hpp
    src/app/ui/views/settings/input_settings_view.cpp
    src/app/ui/views/settings/input_settings_view.hpp
    src/app/ui/views/settings/ipl_settings_view.cpp
    src/app/ui/views/settings/ipl_settings_view.hpp
    src/app/ui/views/settings/mission_stick_config_view.cpp
    src/app/ui/views/settings/mission_stick_config_view.hpp
    src/app/ui/views/settings/settings_view_base.cpp
    src/app/ui/views/settings/settings_view_base.hpp
    src/app/ui/views/settings/shuttle_mouse_config_view.cpp
    src/app/ui/views/settings/shuttle_mouse_config_view.hpp
    src/app/ui/views/settings/system_settings_view.cpp
    src/app/ui/views/settings/system_settings_view.hpp
    src/app/ui/views/settings/video_settings_view.cpp
    src/app/ui/views/settings/video_settings_view.hpp
    src/app/ui/views/settings/virtua_gun_config_view.cpp
    src/app/ui/views/settings/virtua_gun_config_view.hpp
    src/app/ui/views/settings/tweaks_settings_view.cpp
    src/app/ui/views/settings/tweaks_settings_view.hpp

    src/app/ui/widgets/audio_widgets.cpp
    src/app/ui/widgets/audio_widgets.hpp
    src/app/ui/widgets/cartridge_widgets.cpp
    src/app/ui/widgets/cartridge_widgets.hpp
    src/app/ui/widgets/common_widgets.cpp
    src/app/ui/widgets/common_widgets.hpp
    src/app/ui/widgets/datetime_widgets.cpp
    src/app/ui/widgets/datetime_widgets.hpp
    src/app/ui/widgets/debug_widgets.cpp
    src/app/ui/widgets/debug_widgets.hpp
    src/app/ui/widgets/input_widgets.cpp
    src/app/ui/widgets/input_widgets.hpp
    src/app/ui/widgets/peripheral_widgets.cpp
    src/app/ui/widgets/peripheral_widgets.hpp
    src/app/ui/widgets/savestate_widgets.cpp
    src/app/ui/widgets/savestate_widgets.hpp
    src/app/ui/widgets/settings_widgets.cpp
    src/app/ui/widgets/settings_widgets.hpp
    src/app/ui/widgets/system_widgets.cpp
    src/app/ui/widgets/system_widgets.hpp
    src/app/ui/widgets/unbound_actions_widget.cpp
    src/app/ui/widgets/unbound_actions_widget.hpp

    src/app/ui/windows/about_window.cpp
    src/app/ui/windows/about_window.hpp
    src/app/ui/windows/backup_ram_manager_window.cpp
    src/app/ui/windows/backup_ram_manager_window.hpp
    src/app/ui/windows/message_history_window.cpp
    src/app/ui/windows/message_history_window.hpp
    src/app/ui/windows/peripheral_config_window.cpp
    src/app/ui/windows/peripheral_config_window.hpp
    src/app/ui/windows/settings_window.cpp
    src/app/ui/windows/settings_window.hpp
    src/app/ui/windows/system_state_window.cpp
    src/app/ui/windows/system_state_window.hpp
    src/app/ui/windows/update_onboarding_window.cpp
    src/app/ui/windows/update_onboarding_window.hpp
    src/app/ui/windows/update_window.cpp
    src/app/ui/windows/update_window.hpp

    src/app/ui/windows/debug/cdblock_cmd_trace_window.cpp
    src/app/ui/windows/debug/cdblock_cmd_trace_window.hpp
    src/app/ui/windows/debug/cdblock_drive_state_trace_window.cpp
    src/app/ui/windows/debug/cdblock_drive_state_trace_window.hpp
    src/app/ui/windows/debug/cdblock_filters_window.cpp
    src/app/ui/windows/debug/cdblock_filters_window.hpp
    src/app/ui/windows/debug/cdblock_partitions_window.cpp
    src/app/ui/windows/debug/cdblock_partitions_window.hpp
    src/app/ui/windows/debug/cdblock_window_base.cpp
    src/app/ui/windows/debug/cdblock_window_base.hpp
    src/app/ui/windows/debug/cdblock_window_set.hpp
    src/app/ui/windows/debug/cdblock_ygr_cmd_trace_window.cpp
    src/app/ui/windows/debug/cdblock_ygr_cmd_trace_window.hpp
    src/app/ui/windows/debug/debug_output_window.cpp
    src/app/ui/windows/debug/debug_output_window.hpp
    src/app/ui/windows/debug/memory_viewer_window.cpp
    src/app/ui/windows/debug/memory_viewer_window.hpp
    src/app/ui/windows/debug/scsp_kyonex_trace_window.cpp
    src/app/ui/windows/debug/scsp_kyonex_trace_window.hpp
    src/app/ui/windows/debug/scsp_output_window.cpp
    src/app/ui/windows/debug/scsp_output_window.hpp
    src/app/ui/windows/debug/scsp_slots_window.cpp
    src/app/ui/windows/debug/scsp_slots_window.hpp
    src/app/ui/windows/debug/scsp_window_base.cpp
    src/app/ui/windows/debug/scsp_window_base.hpp
    src/app/ui/windows/debug/scsp_window_set.hpp
    src/app/ui/windows/debug/scu_dma_trace_window.cpp
    src/app/ui/windows/debug/scu_dma_trace_window.hpp
    src/app/ui/windows/debug/scu_dma_window.cpp
    src/app/ui/windows/debug/scu_dma_window.hpp
    src/app/ui/windows/debug/scu_dsp_window.cpp
    src/app/ui/windows/debug/scu_dsp_window.hpp
    src/app/ui/windows/debug/scu_interrupt_trace_window.cpp
    src/app/ui/windows/debug/scu_interrupt_trace_window.hpp
    src/app/ui/windows/debug/scu_registers_window.cpp
    src/app/ui/windows/debug/scu_registers_window.hpp
    src/app/ui/windows/debug/scu_window_set.hpp
    src/app/ui/windows/debug/sh2_breakpoints_window.cpp
    src/app/ui/windows/debug/sh2_breakpoints_window.hpp
    src/app/ui/windows/debug/sh2_cache_window.cpp
    src/app/ui/windows/debug/sh2_cache_window.hpp
    src/app/ui/windows/debug/sh2_debugger_window.cpp
    src/app/ui/windows/debug/sh2_debugger_window.hpp
    src/app/ui/windows/debug/sh2_divu_window.cpp
    src/app/ui/windows/debug/sh2_divu_window.hpp
    src/app/ui/windows/debug/sh2_dmac_trace_window.cpp
    src/app/ui/windows/debug/sh2_dmac_trace_window.hpp
    src/app/ui/windows/debug/sh2_dmac_window.cpp
    src/app/ui/windows/debug/sh2_dmac_window.hpp
    src/app/ui/windows/debug/sh2_exception_vectors_window.cpp
    src/app/ui/windows/debug/sh2_exception_vectors_window.hpp
    src/app/ui/windows/debug/sh2_interrupt_trace_window.cpp
    src/app/ui/windows/debug/sh2_interrupt_trace_window.hpp
    src/app/ui/windows/debug/sh2_interrupts_window.cpp
    src/app/ui/windows/debug/sh2_interrupts_window.hpp
    src/app/ui/windows/debug/sh2_power_window.cpp
    src/app/ui/windows/debug/sh2_power_window.hpp
    src/app/ui/windows/debug/sh2_timers_window.cpp
    src/app/ui/windows/debug/sh2_timers_window.hpp
    src/app/ui/windows/debug/sh2_watchpoints_window.cpp
    src/app/ui/windows/debug/sh2_watchpoints_window.hpp
    src/app/ui/windows/debug/sh2_window_base.cpp
    src/app/ui/windows/debug/sh2_window_base.hpp
    src/app/ui/windows/debug/sh2_window_set.hpp
    src/app/ui/windows/debug/vdp_window_base.cpp
    src/app/ui/windows/debug/vdp_window_base.hpp
    src/app/ui/windows/debug/vdp_window_set.hpp
    src/app/ui/windows/debug/vdp1_registers_window.cpp
    src/app/ui/windows/debug/vdp1_registers_window.hpp
    src/app/ui/windows/debug/vdp2_bg_layer_params_window.cpp
    src/app/ui/windows/debug/vdp2_bg_layer_params_window.hpp
    src/app/ui/windows/debug/vdp2_color_calc_params_window.cpp
    src/app/ui/windows/debug/vdp2_color_calc_params_window.hpp
    src/app/ui/windows/debug/vdp2_cram_window.cpp
    src/app/ui/windows/debug/vdp2_cram_window.hpp
    src/app/ui/windows/debug/vdp2_debug_overlay_window.cpp
    src/app/ui/windows/debug/vdp2_debug_overlay_window.hpp
    src/app/ui/windows/debug/vdp2_layer_visibility_window.cpp
    src/app/ui/windows/debug/vdp2_layer_visibility_window.hpp
    src/app/ui/windows/debug/vdp2_registers_window.cpp
    src/app/ui/windows/debug/vdp2_registers_window.hpp
    src/app/ui/windows/debug/vdp2_sprite_layer_params_window.cpp
    src/app/ui/windows/debug/vdp2_sprite_layer_params_window.hpp
    src/app/ui/windows/debug/vdp2_vram_access_patterns_window.cpp
    src/app/ui/windows/debug/vdp2_vram_access_patterns_window.hpp
    src/app/ui/windows/debug/vdp2_window_params_window.cpp
    src/app/ui/windows/debug/vdp2_window_params_window.hpp

    src/serdes/cereal_archive_vector.hpp
    src/serdes/cereal_savestate.hpp

    src/util/file_loader.cpp
    src/util/file_loader.hpp
    src/util/math.hpp
    src/util/os_exception_handler.cpp
    src/util/os_exception_handler.hpp
    src/util/os_features.cpp
    src/util/os_features.hpp
    src/util/regions.cpp
    src/util/regions.hpp
    src/util/ring_buffer.hpp
    src/util/rom_loader.cpp
    src/util/rom_loader.hpp
    src/util/sdl_file_dialog.hpp
    src/util/service_locator.hpp
    src/util/stdio_suppress.cpp
    src/util/stdio_suppress.hpp
    src/util/std_lib.cpp
    src/util/std_lib.hpp
)
add_executable(ymir::ymir-sdl3 ALIAS ymir-sdl3)

## Add platform-specific graphics context implementations
if (WIN32)
    target_sources(ymir-sdl3 PRIVATE
        src/app/services/gfx/gfx_context_impl_d3d11.cpp
        src/app/services/gfx/gfx_context_impl_d3d11.hpp
        src/app/services/gfx/gfx_context_impl_d3d12.cpp
        src/app/services/gfx/gfx_context_impl_d3d12.hpp
        src/app/services/gfx/gfx_context_spec_d3d12.hpp
        src/app/services/gfx/gfx_d3d_utils.cpp
        src/app/services/gfx/gfx_d3d_utils.hpp
    )
endif ()

if (Vulkan_FOUND)
    target_sources(ymir-sdl3 PRIVATE
        src/app/services/gfx/gfx_context_impl_vulkan.cpp
        src/app/services/gfx/gfx_context_impl_vulkan.hpp
        src/app/services/gfx/gfx_context_spec_vulkan.hpp
        src/app/services/gfx/gfx_vulkan_utils.cpp
        src/app/services/gfx/gfx_vulkan_utils.hpp
    )
    target_link_libraries(ymir-sdl3 PRIVATE
        Vulkan::Vulkan
    )
endif ()

if (APPLE)
    target_sources(ymir-sdl3 PRIVATE
        src/app/services/gfx/gfx_context_impl_metal.mm
        src/app/services/gfx/gfx_context_impl_metal.hpp
        src/app/services/gfx/gfx_context_spec_metal.hpp
        src/app/services/gfx/gfx_metal_utils.mm
        src/app/services/gfx/gfx_metal_utils.hpp
    )

    set_source_files_properties(
        src/app/services/gfx/gfx_context_impl_metal.mm
        src/app/services/gfx/gfx_metal_utils.mm
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
endif ()

## Compile shaders
set(SHADER_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(VERTEX_SHADER_SOURCES
    res/shaders/gctx/quad_vs.hlsl
)
set(PIXEL_SHADER_SOURCES
    res/shaders/gctx/quad_ps.hlsl
)
## TODO: add per-platform shader code if needed
# if (WIN32)
#     list(APPEND VERTEX_SHADER_SOURCES
#         res/shaders/d3d12/some_shader_vs.hlsl
#     )
#     list(APPEND PIXEL_SHADER_SOURCES
#         res/shaders/d3d12/some_shader_ps.hlsl
#     )
# elseif (APPLE)
# elseif (UNIX) # Linux and FreeBSD
# endif ()

set(COMPILED_SHADERS "")
foreach (SHADER_SRC ${VERTEX_SHADER_SOURCES})
    compile_shader(
        OUT_PATHS SHADER_BINARY_FILES
        SOURCE "${SHADER_SRC}"
        WHENCE "${SHADER_INPUT_DIR}"
        DESTINATION "${SHADER_OUTPUT_DIR}"
        ENTRYPOINT "VSMain"
        PROFILE "vs_6_0"
    )
    list(APPEND COMPILED_SHADERS ${SHADER_BINARY_FILES})
endforeach ()
foreach (SHADER_SRC ${PIXEL_SHADER_SOURCES})
    compile_shader(
        OUT_PATHS SHADER_BINARY_FILES
        SOURCE "${SHADER_SRC}"
        WHENCE "${SHADER_INPUT_DIR}"
        DESTINATION "${SHADER_OUTPUT_DIR}"
        ENTRYPOINT "PSMain"
        PROFILE "ps_6_0"
    )
    list(APPEND COMPILED_SHADERS ${SHADER_BINARY_FILES})
endforeach ()

## Create the embedded shaders target
MESSAGE(STATUS "Generating SDL3 frontend shader resource library: ${COMPILED_SHADERS}")
cmrc_add_resource_library(ymir-sdl3-shaders
    ALIAS ymir-sdl3::shaders
    NAMESPACE ymir_sdl3_shaders
    WHENCE "${SHADER_OUTPUT_DIR}"
    ${COMPILED_SHADERS}
)

## Set executable version
set_target_properties(ymir-sdl3 PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})

if(APPLE)
    # Configure Info.plist
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/res/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

    # Set Apple-specific bundle properties
    set_target_properties(ymir-sdl3 PROPERTIES
                          MACOSX_BUNDLE TRUE
                          MACOSX_BUNDLE_BUNDLE_NAME "Ymir"
                          MACOSX_BUNDLE_BUNDLE_VERSION "${Ymir_VERSION}"
                          MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.strikerx3.ymir"
                          MACOSX_BUNDLE_ICON_FILE "ymir.icns"
                          MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
                          MACOSX_BUNDLE_SHORT_VERSION_STRING "${Ymir_VERSION}")

    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/res/ymir.icns PROPERTIES
                                MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(ymir-sdl3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/res/ymir.icns)
endif()

find_package(cereal CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(rtmidi CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

target_include_directories(ymir-sdl3
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_link_libraries(ymir-sdl3 PRIVATE
    ymir::ymir-core
    ymir-sdl3::rc
    ymir-sdl3::shaders
    SDL3::SDL3
    date::date
    fmt::fmt
    cxxopts::cxxopts
    imgui::imgui
    tomlplusplus::tomlplusplus
    cereal::cereal
    lz4::lz4
    discord-rpc
    RtMidi::rtmidi
    CURL::libcurl_static
    stb::stb
    dr_libs::dr_libs
)
if (WIN32)
    target_link_libraries(ymir-sdl3 PRIVATE
        Dwmapi

        WIL::WIL
    )
endif ()
target_compile_features(ymir-sdl3 PUBLIC cxx_std_20)

## Create embedded resource library
cmrc_add_resource_library(ymir-sdl3-rc
    ALIAS ymir-sdl3::rc
    NAMESPACE Ymir_sdl3_rc
    WHENCE res/embed

    res/embed/fonts/MaterialSymbolsOutlined_Filled-Regular.ttf
    res/embed/fonts/MPLUSU-Ymir-Bold.ttf
    res/embed/fonts/MPLUSU-Ymir-ExtraBold.ttf
    res/embed/fonts/SplineSans-Bold.ttf
    res/embed/fonts/SplineSans-Medium.ttf
    res/embed/fonts/SplineSansMono-Bold.ttf
    res/embed/fonts/SplineSansMono-Medium.ttf
    res/embed/fonts/ZenDots-Regular.ttf

    res/embed/images/ymir.png
)

cmrk_copy_runtime_dlls(ymir-sdl3)

## Generate MacOS mig IPC implementation
if (APPLE)
    find_path(MACH_EXC_DEFS_DIR "mach/mach_exc.defs")
    message(STATUS "mach/mach_exc.defs location: ${MACH_EXC_DEFS_DIR}")
    if(MACH_EXC_DEFS_DIR)
        execute_process(
            COMMAND
                mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64"
            COMMAND mig
                -arch x86_64
                -server "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64/mach_exc_server.c"
                -sheader "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64/mach_exc_server.h"
                "${MACH_EXC_DEFS_DIR}/mach/mach_exc.defs"
        )
        execute_process(
            COMMAND
                mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64"
            COMMAND mig
                -arch arm64
                -server "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64/mach_exc_server.c"
                -sheader "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64/mach_exc_server.h"
                "${MACH_EXC_DEFS_DIR}/mach/mach_exc.defs"
        )
        target_sources(ymir-sdl3 PRIVATE
            src/util/mig/macos_mig.c
        )
    endif()
endif()

## Add icon
if (WIN32)
    target_sources(ymir-sdl3 PRIVATE res/ymir.rc)
endif ()

## Make it a Win32 executable (without a console window) on Windows
if (WIN32 AND NOT Ymir_ENABLE_DEVLOG)
    set_target_properties(ymir-sdl3 PROPERTIES WIN32_EXECUTABLE ON)
endif ()

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-sdl3")
    set_property(TARGET ymir-sdl3 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Define additional macros
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_ENABLE_IMGUI_DEMO=$<BOOL:${Ymir_ENABLE_IMGUI_DEMO}>)
target_compile_definitions(ymir-core PUBLIC Ymir_ENABLE_UPDATE_CHECKS=$<BOOL:${Ymir_ENABLE_UPDATE_CHECKS}>)
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_ORGANIZATION_NAME="StrikerX3")
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_APP_NAME="${PROJECT_NAME}")

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-sdl3 PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-sdl3 PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-sdl3)
    set_target_properties(ymir-sdl3 PROPERTIES FOLDER "Ymir")
    set_target_properties(ymir-sdl3-rc PROPERTIES FOLDER "Ymir")
endif ()
