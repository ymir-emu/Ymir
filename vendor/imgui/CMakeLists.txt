add_library(imgui
	ymir/imgui_memory_editor.h
	ymir/ymir_imconfig.h

	imgui/imgui.cpp
	imgui/imgui.h
	imgui/imgui_draw.cpp
	imgui/imgui_internal.h
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/imstb_rectpack.h
	imgui/imstb_textedit.h
	imgui/imstb_truetype.h

	imgui/backends/imgui_impl_sdl3.cpp
	imgui/backends/imgui_impl_sdl3.h
	#imgui/backends/imgui_impl_sdlgpu3.cpp
	#imgui/backends/imgui_impl_sdlgpu3.h
	#imgui/backends/imgui_impl_sdlgpu3_shaders.h
	imgui/backends/imgui_impl_sdlrenderer3.cpp
	imgui/backends/imgui_impl_sdlrenderer3.h
	# TODO: add other backends as needed

	imgui/misc/cpp/imgui_stdlib.cpp
	imgui/misc/cpp/imgui_stdlib.h
)

if (Ymir_ENABLE_IMGUI_DEMO)
	target_sources(imgui PRIVATE
		imgui/imgui_demo.cpp
	)
endif ()

## Add graphics backends for each platform
if (WIN32)
	target_sources(imgui
		PRIVATE
			imgui/backends/imgui_impl_dx11.cpp
			imgui/backends/imgui_impl_dx12.cpp
		PUBLIC
			imgui/backends/imgui_impl_dx11.h
			imgui/backends/imgui_impl_dx12.h
	)
elseif (APPLE)
	target_sources(imgui
		PRIVATE
			imgui/backends/imgui_impl_metal.mm
		PUBLIC
			imgui/backends/imgui_impl_metal.h
	)
endif ()

## Add Vulkan if installed on the system
if (Vulkan_FOUND)
	target_sources(imgui
		PRIVATE
			imgui/backends/imgui_impl_vulkan.cpp
		PUBLIC
			imgui/backends/imgui_impl_vulkan.h
	)
endif ()

if (MSVC)
	target_sources(imgui PRIVATE
		imgui/misc/debuggers/imgui.natstepfilter
		imgui/misc/debuggers/imgui.natvis
	)
endif ()

add_library(imgui::imgui ALIAS imgui)
target_include_directories(imgui
    PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/imgui>"
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ymir>"
    PRIVATE
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/imgui>"
)
target_compile_features(imgui PUBLIC cxx_std_20)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="ymir_imconfig.h")

target_link_libraries(imgui PRIVATE SDL3::SDL3)
if (Vulkan_FOUND)
	target_link_libraries(imgui PRIVATE Vulkan::Vulkan)
endif ()
