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
    message(FATAL_ERROR "Set -DTORCH_ROOT=/path/to/site-packages/torch (see tools/setup_toolchain.sh / tools/setup_toolchain_windows.ps1)")
endif()

set(TORCH_INSTALL_PREFIX "${TORCH_ROOT}")
set(TORCH_INCLUDE_DIRS "${TORCH_ROOT}/include" "${TORCH_ROOT}/include/torch/csrc/api/include")

if(WIN32)
    # The Windows PyPI torch wheel is built with MSVC, same as this project
    # will be - no ABI-flag workaround needed here (that was purely a GCC/
    # libstdc++ issue affecting the Linux wheel, see the else branch below).
    # Link against the import libs (.lib); the matching .dlls are copied
    # next to the built plugin below so Windows' standard "look beside the
    # loading module first" DLL search order finds them at load time.
    set(TORCH_LIBRARIES "${TORCH_ROOT}/lib/torch_cpu.lib" "${TORCH_ROOT}/lib/c10.lib")
    set(TORCH_CXX_FLAGS "")
    # torch's Windows wheel also ships a torch/bin/ folder duplicating a
    # couple of lib/*.dll files (asmjit.dll, fbgemm.dll) - unclear why two
    # copies exist, but copying both is a safe, low-cost hedge while a real
    # user's load failure (error 126 on torch_cpu.dll/fbgemm.dll,
    # confirmed NOT caused by a missing file or the VC++ redistributable)
    # is investigated further.
    file(GLOB TORCH_RUNTIME_DLLS "${TORCH_ROOT}/lib/*.dll" "${TORCH_ROOT}/bin/*.dll")
else()
    set(TORCH_LIBRARIES "${TORCH_ROOT}/lib/libtorch_cpu.so" "${TORCH_ROOT}/lib/libc10.so")
    # This PyPI build of libtorch_cpu.so was compiled against the old
    # (pre-C++11) libstdc++ string/list ABI - every translation unit that
    # includes a Torch header must agree, or symbol resolution fails at link
    # time (see tools/setup_toolchain.sh for how this was diagnosed).
    set(TORCH_CXX_FLAGS "-D_GLIBCXX_USE_CXX11_ABI=0")
endif()

# -----------------------------------------------------------------------------
# JUCE
# -----------------------------------------------------------------------------
add_subdirectory(libs/JUCE)

juce_add_plugin(HallucinatorRAVE
    COMPANY_NAME "Sprizzle"
    COMPANY_WEBSITE "https://sprizzle.io"
    COMPANY_EMAIL "spryte@sprizzle.io"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Sprizzle"
    BUNDLE_ID "io.sprizzle.hallucinatorrave"
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

# On Windows, juce_add_plugin() turns the above into the PE version-resource
# fields (CompanyName, LegalCopyright, ProductName, ...) that Explorer's
# file-properties dialog and Windows SmartScreen show - this is the
# "developer info" half of the bureaucracy the build brief/README ask about.
# The other half (actually signing the binary) is Windows-tooling-only
# (signtool + a certificate) and can't be done from this Linux container -
# see tools/sign_windows_adhoc.ps1, which has to be run on Windows.

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
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_include_directories(HallucinatorRAVE PRIVATE Source)

# Torch's headers trigger warnings under JUCE's strict warning flags; treat
# them as system headers so only our own code is held to that standard.
target_include_directories(HallucinatorRAVE SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})

# Not using juce_recommended_lto_flags: LTO's cross-TU ODR checker is what
# surfaced the ABI-propagation bug fixed above, but it also emits a
# "serial compilation of N LTRANS jobs" note unless further tuned, and LTO
# buys nothing for a plugin this size. Skipping it keeps the build both
# warning-free and faster to iterate on.
target_link_libraries(HallucinatorRAVE
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ${TORCH_LIBRARIES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

separate_arguments(TORCH_CXX_FLAGS_LIST UNIX_COMMAND "${TORCH_CXX_FLAGS}")

# PUBLIC, not PRIVATE: juce_add_plugin() creates a separate HallucinatorRAVE_VST3
# wrapper target that compiles its own JUCE plugin-client sources and links
# PRIVATE against this one. A consuming target still needs this target's
# *interface* compile options to build its own sources consistently - if the
# ABI define were PRIVATE, HallucinatorRAVE_VST3's JUCE wrapper code would
# compile with the default (new) ABI while this target's code used the old
# one, and juce::String (built with two different mangled constructor names)
# would fail to link - or worse, silently violate the ODR - at the seam
# between the two targets. This was caught by -flto's ODR checker; the
# underlying bug exists with or without LTO.
target_compile_options(HallucinatorRAVE PUBLIC ${TORCH_CXX_FLAGS_LIST})

# -Wno-deprecated-copy is GCC/Clang syntax and doesn't exist for MSVC's
# cl.exe (which fails outright with "D8021: invalid numeric argument" if
# handed it - this broke the first successful-configure Windows CI build).
# Also scoped to CXX only: JUCE compiles a couple of C sources (e.g.
# Sheenbidi) into this target, and GCC warns if a C++-only warning flag is
# passed while compiling those.
target_compile_options(HallucinatorRAVE PRIVATE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Wno-deprecated-copy>)

# Runtime dependencies (libtorch_cpu.so, libc10.so, the cudart/cupti shims
# libtorch_cpu.so needs to dynamically link even though CUDA is never used)
# live next to the plugin binary in the packaged dist/ output, not system-wide.
# $ORIGIN-relative rpaths let the plugin find them wherever the bundle ends up.
if(UNIX AND NOT APPLE)
    set_target_properties(HallucinatorRAVE_VST3 PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
endif()

# Windows has no rpath mechanism, and - unlike what $ORIGIN achieves on Linux
# above - copying torch's .dlls next to the built .vst3 does NOT make them
# findable on its own: Windows' standard DLL search order for a *dependent*
# DLL only checks the hosting process's own directory (e.g. the DAW's .exe
# folder) and PATH, never "the directory the requesting DLL itself lives
# in". That gap is invisible in this project's own dev/CI loop (which
# happens to put torch's lib dir on PATH for its own build/test
# convenience) but reproduces as a hard failure to load under a real DAW
# hosting the plugin from its own install location - confirmed via a user
# report of FL Studio showing a generic scan "error" (no vendor/category)
# because the plugin DLL itself never finished loading.
#
# A first attempt fixed this via delay-loading (see git history), but hit a
# hard MSVC limitation: c10.dll exports a data symbol, and delay-loading can
# only defer function calls, never raw data-symbol access (LNK1194 "cannot
# delay-load 'c10.dll' due to import of data symbol ...") - confirmed via
# CI's isolated-directory smoke test, dropping c10.dll's /DELAYLOAD entry
# just reproduced the original failure for c10.dll specifically.
#
# Fixed instead with a dependency-free "shim" binary (Source/Windows/
# PluginShim.cpp) that IS the file a DAW's LoadLibraryW() resolves at
# Contents/x86_64-win/HallucinatorRAVE.vst3: it explicitly LoadLibraryExW's
# the real implementation (renamed below to HallucinatorRAVE_Impl.dll) with
# LOAD_WITH_ALTERED_SEARCH_PATH, which makes the loader search that same
# directory for every one of the impl DLL's imports - function or data -
# as ordinary PE loading, not delay-load's more limited mechanism. The
# shim itself has zero torch/JUCE dependencies, so its own load always
# succeeds regardless of the host's search path.
if(WIN32)
    add_library(HallucinatorRAVE_Shim MODULE Source/Windows/PluginShim.cpp)
    set_target_properties(HallucinatorRAVE_Shim PROPERTIES OUTPUT_NAME "HallucinatorRAVE_Shim")

    add_custom_command(TARGET HallucinatorRAVE_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TORCH_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:HallucinatorRAVE_VST3>
        COMMENT "Copying LibTorch runtime DLLs next to HallucinatorRAVE.vst3")

    add_custom_command(TARGET HallucinatorRAVE_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename
                $<TARGET_FILE:HallucinatorRAVE_VST3>
                $<TARGET_FILE_DIR:HallucinatorRAVE_VST3>/HallucinatorRAVE_Impl.dll
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:HallucinatorRAVE_Shim>
                $<TARGET_FILE:HallucinatorRAVE_VST3>
        COMMENT "Renaming the real plugin binary to HallucinatorRAVE_Impl.dll and installing the dependency-free shim in its place"
        DEPENDS HallucinatorRAVE_Shim)

    # Standalone diagnostic (not shipped, not part of dist/) - see its own
    # top-of-file comment. Built as a normal CI artifact so it can be handed
    # to a user hitting a load failure CI itself can't reproduce, to run on
    # their own affected machine. Header-only use of the VST3 SDK's
    # interface declarations - no torch/JUCE dependency needed.
    add_executable(vst3_load_diagnostic tools/vst3_load_diagnostic.cpp)
    target_include_directories(vst3_load_diagnostic PRIVATE
        libs/JUCE/modules/juce_audio_processors/format_types/VST3_SDK)
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
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(offline_render_test PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        ${TORCH_LIBRARIES})
    set_target_properties(offline_render_test PROPERTIES
        BUILD_RPATH "${TORCH_INSTALL_PREFIX}/lib")
endif()
