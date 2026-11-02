cmake_minimum_required(VERSION 3.25.0)

# F. Klangmacher - circuit-level emulation of a 1979 analog string/poly/mono keyboard as VST3 plugin (Steinberg VST3 SDK, no JUCE)
#   Linux:   cmake -S vst3 -B ~/build/fklangmacher -G Ninja -DCMAKE_BUILD_TYPE=Release
#   Windows: cmake -S vst3 -B ~/build/fklangmacher-win -G Ninja -DCMAKE_BUILD_TYPE=Release
#                  -DCMAKE_TOOLCHAIN_FILE=vst3/cmake/mingw-w64.cmake        (32 bit: mingw-w64-i686.cmake)
#   macOS:   see README ("macOS bauen")

option(SMK_BUILD_PLUGIN "Build the VST3 plugin (needs the VST3 SDK)" ON)
set(VST3_SDK_ROOT "$ENV{HOME}/SDKs/vst3sdk" CACHE PATH "Steinberg VST3 SDK folder")

project(FKlangmacher VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#------------------------------------------------------------------ sound model (no SDK)
add_library(smk_core STATIC
    src/core/params.cpp
    src/core/presets.cpp
    src/core/device.cpp
    src/core/poly.cpp
    src/core/mono.cpp
    src/core/synth.cpp
)
target_include_directories(smk_core PUBLIC src/core)
target_compile_options(smk_core PUBLIC $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off -O2>)

# offline renderer and measurements (no DAW needed)
add_executable(smk_test test/coretest.cpp)
target_link_libraries(smk_test PRIVATE smk_core)
if(MINGW)
    target_link_options(smk_test PRIVATE -static)
endif()

#------------------------------------------------------------------ VST3 plugin
if(SMK_BUILD_PLUGIN)
    include(cmake/plugin.cmake)
endif()
