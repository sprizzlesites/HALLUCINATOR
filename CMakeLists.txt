cmake_minimum_required(VERSION 3.22)

project(HallucinatorRAVE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# LibTorch
#
# Deliberately NOT using find_package(Torch): TorchConfig.cmake pulls in
# Caffe2Config.cmake, which - because the PyPI wheel this project's
# tools/setup_toolchain.sh installs was built with USE_CUDA=1 - unconditionally
# hard-requires a full CUDA toolkit (nvcc, cuda.h, ...) to be found at CMake
# configure time, even though this project never links libtorch_cuda.so and
# never calls a CUDA op. Rather than installing a multi-GB CUDA toolkit purely
# to satisfy that configure-time check, point directly at the two shared
# libraries this project actually needs (proven to link and run standalone in
# tools/setup_toolchain.sh's smoke test): libtorch_cpu.so and libc10.so.
#
# Point -DTORCH_ROOT at the torch/ site-packages directory, e.g.:
#   cmake -B build -G Ninja -DTORCH_ROOT=$(pwd)/libs/torch-venv/lib/python3.11/site-packages/torch
# -----------------------------------------------------------------------------
set(TORCH_ROOT "" CACHE PATH "Path to the torch/ site-packages directory (containing include/ and lib/)")

if(NOT TORCH_ROOT)
    message(FATAL_ERROR "Set -DTORCH_ROOT=/path/to/site-packages/torch (see tools/setup_toolchain.sh)")
endif()

set(TORCH_INSTALL_PREFIX "${TORCH_ROOT}")
set(TORCH_INCLUDE_DIRS "${TORCH_ROOT}/include" "${TORCH_ROOT}/include/torch/csrc/api/include")
set(TORCH_LIBRARIES "${TORCH_ROOT}/lib/libtorch_cpu.so" "${TORCH_ROOT}/lib/libc10.so")
# This PyPI build of libtorch_cpu.so was compiled against the old
# (pre-C++11) libstdc++ string/list ABI - every translation unit that
# includes a Torch header must agree, or symbol resolution fails at link
# time (see tools/setup_toolchain.sh for how this was diagnosed).
set(TORCH_CXX_FLAGS "-D_GLIBCXX_USE_CXX11_ABI=0")

# -----------------------------------------------------------------------------
# JUCE
# -----------------------------------------------------------------------------
add_subdirectory(libs/JUCE)

juce_add_plugin(HallucinatorRAVE
    COMPANY_NAME "Sprizzle"
    PLUGIN_MANUFACTURER_CODE Sprz
    PLUGIN_CODE Hrav
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3
    PRODUCT_NAME "HallucinatorRAVE")

juce_generate_juce_header(HallucinatorRAVE)

target_sources(HallucinatorRAVE
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/RaveModel.cpp
        Source/LatentEngine.cpp)

target_compile_definitions(HallucinatorRAVE
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_include_directories(HallucinatorRAVE PRIVATE Source)

# Torch's headers trigger warnings under JUCE's strict warning flags; treat
# them as system headers so only our own code is held to that standard.
target_include_directories(HallucinatorRAVE SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})

target_link_libraries(HallucinatorRAVE
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ${TORCH_LIBRARIES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

separate_arguments(TORCH_CXX_FLAGS_LIST UNIX_COMMAND "${TORCH_CXX_FLAGS}")
target_compile_options(HallucinatorRAVE PRIVATE ${TORCH_CXX_FLAGS_LIST})

# Silence a couple of warnings that fire from JUCE/Torch header interaction
# on this toolchain (unused parameters in Torch's own headers, and
# deprecated-copy notices from ATen's Tensor wrapper types pulled in via
# system headers still get flagged by -Wall on some GCC versions since they
# originate in macro-expanded code attributed to our translation unit).
target_compile_options(HallucinatorRAVE PRIVATE -Wno-deprecated-copy)

# Runtime dependencies (libtorch_cpu.so, libc10.so, the cudart/cupti shims
# libtorch_cpu.so needs to dynamically link even though CUDA is never used)
# live next to the plugin binary in the packaged dist/ output, not system-wide.
# $ORIGIN-relative rpaths let the plugin find them wherever the bundle ends up.
if(UNIX AND NOT APPLE)
    set_target_properties(HallucinatorRAVE_VST3 PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
endif()

# -----------------------------------------------------------------------------
# Dev tools (not shipped): dummy model generator + offline render test harness
# -----------------------------------------------------------------------------
option(HALLUCINATOR_BUILD_TOOLS "Build dev tools (dummy model generator, offline render test)" ON)

if(HALLUCINATOR_BUILD_TOOLS)
    add_executable(make_dummy_rave_model tools/make_dummy_rave_model.cpp)
    target_link_libraries(make_dummy_rave_model PRIVATE ${TORCH_LIBRARIES})
    target_include_directories(make_dummy_rave_model SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})
    target_compile_options(make_dummy_rave_model PRIVATE ${TORCH_CXX_FLAGS_LIST})
    set_target_properties(make_dummy_rave_model PROPERTIES
        BUILD_RPATH "${TORCH_INSTALL_PREFIX}/lib")

    add_executable(offline_render_test
        tests/offline_render_test.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/RaveModel.cpp
        Source/LatentEngine.cpp
        Source/Params.cpp)
    target_include_directories(offline_render_test PRIVATE Source)
    target_include_directories(offline_render_test SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})
    target_compile_options(offline_render_test PRIVATE ${TORCH_CXX_FLAGS_LIST})
    target_compile_definitions(offline_render_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(offline_render_test PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        ${TORCH_LIBRARIES})
    set_target_properties(offline_render_test PROPERTIES
        BUILD_RPATH "${TORCH_INSTALL_PREFIX}/lib")
endif()
