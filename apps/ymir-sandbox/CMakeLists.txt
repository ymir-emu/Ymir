## Create the executable target
add_executable(ymir-sandbox
    src/main.cpp

    src/sandbox_bin_cue_loader.cpp
    src/sandbox_bup.cpp
    src/sandbox_curl.cpp
    src/sandbox_deadlock.cpp
    src/sandbox_disc_info_extractor.cpp
    src/sandbox_host_cd.cpp
    src/sandbox_input.cpp
    src/sandbox_sh2_perf.cpp
    src/sandbox_vdp1_accuracy.cpp
    src/sandbox_vdp1_poly.cpp
)
add_executable(ymir::ymir-sandbox ALIAS ymir-sandbox)
set_target_properties(ymir-sandbox PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_include_directories(ymir-sandbox
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(ymir-sandbox PUBLIC cxx_std_20)

find_package(date CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

## Add dependencies
target_link_libraries(ymir-sandbox PRIVATE
    ymir::ymir-core
    SDL3::SDL3
    date::date
    fmt::fmt
    CURL::libcurl_static
    nlohmann_json::nlohmann_json
    stb::stb
)

cmrk_copy_runtime_dlls(ymir-sandbox)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-sandbox")
    set_property(TARGET ymir-sandbox PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-sandbox PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-sandbox PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-sandbox)
    set_target_properties(ymir-sandbox PROPERTIES FOLDER "Ymir")
endif ()

## No packaging for this project as it's meant to be testing grounds
