## Create the executable target
add_executable(ymir-core-tests
    src/hw/scu/scu_dsp_tests.cpp

    src/hw/sh2/sh2_disasm_tests.cpp
    src/hw/sh2/sh2_divu_tests.cpp
    src/hw/sh2/sh2_intc_tests.cpp
    src/hw/sh2/sh2_macwl_tests.cpp
    src/hw/sh2/sh2_sci_tests.cpp

    src/hw/vdp/vdp_vram_access_patterns_tests.cpp
)
add_executable(ymir::ymir-core-tests ALIAS ymir-core-tests)
set_target_properties(ymir-core-tests PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_link_libraries(ymir-core-tests PRIVATE ymir::ymir-core)
target_compile_features(ymir-core-tests PUBLIC cxx_std_20)

find_package(Catch2 CONFIG REQUIRED)

## Add dependencies
target_link_libraries(ymir-core-tests PRIVATE fmt::fmt Catch2::Catch2WithMain)

cmrk_copy_runtime_dlls(ymir-core-tests)

## Enable LTO if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-core-tests")
    set_property(TARGET ymir-core-tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-core-tests PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-core-tests PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-core-tests)
    set_target_properties(ymir-core-tests PROPERTIES FOLDER "Ymir-tests")
endif ()

## Register Catch2 test with CTest
include(Catch)
catch_discover_tests(ymir-core-tests)

## No packaging for this project as it's meant for unit tests
