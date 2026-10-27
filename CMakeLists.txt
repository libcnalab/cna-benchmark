# SPDX-License-Identifier: MS-PL
#
# CNA Benchmark Suite -- a 3DMark-style benchmark for the CNA framework.
#
# The suite is a plain CNA consumer application: it links the `CNA` umbrella target from a
# sibling CNA checkout and is written against nothing but the public XNA 4.0 surface plus the
# handful of CNAEXT accessors that report which renderer is running and what it supports.
#
# Two build modes, both driven from here:
#
#   * MULTI-RENDERER (default). CNA_BENCHMARK_RENDERERS lists every renderer to compile in;
#     `cna-benchmark --renderer NAME` picks one at process start through
#     CNA::GraphicsRendererSelection. One build tree serves the whole comparison, which is
#     also the reason it is the default: N single-renderer build trees are N full CNA builds.
#   * SINGLE-RENDERER. Set CNA_BENCHMARK_RENDERERS to one identity. Identical source, and the
#     result JSON records which mode produced it.

cmake_minimum_required(VERSION 3.21)

project(CnaBenchmark VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Sibling checkouts -------------------------------------------------------------------
#
# Prefer the original sibling development checkouts when present. The current CNA tree also
# lives beside this lab repository under ../../libcna.

set(_cna_benchmark_default_cna_root "${CMAKE_CURRENT_SOURCE_DIR}/../cnanext")
if(NOT EXISTS "${_cna_benchmark_default_cna_root}/CMakeLists.txt")
    set(_cna_benchmark_default_cna_root "${CMAKE_CURRENT_SOURCE_DIR}/../../libcna/cna")
endif()
set(_cna_benchmark_default_sharp_runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtimenext")
if(NOT EXISTS "${_cna_benchmark_default_sharp_runtime_root}/CMakeLists.txt")
    set(_cna_benchmark_default_sharp_runtime_root
        "${CMAKE_CURRENT_SOURCE_DIR}/../../libcna/sharp-runtime")
endif()
set(CNA_ROOT_DIR "${_cna_benchmark_default_cna_root}" CACHE PATH
    "Path to the CNA framework checkout consumed by this benchmark")
set(CNA_BENCHMARK_SHARP_RUNTIME_ROOT "${_cna_benchmark_default_sharp_runtime_root}" CACHE PATH
    "Path to the sharp-runtime checkout CNA and this benchmark consume")

if(NOT EXISTS "${CNA_ROOT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "CNA was not found at '${CNA_ROOT_DIR}'.\n"
        "Point -DCNA_ROOT_DIR at an existing CNA checkout.")
endif()
if(NOT EXISTS "${CNA_BENCHMARK_SHARP_RUNTIME_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "sharp-runtime was not found at '${CNA_BENCHMARK_SHARP_RUNTIME_ROOT}'.\n"
        "Point -DCNA_BENCHMARK_SHARP_RUNTIME_ROOT at an existing sharp-runtime checkout.")
endif()

# --- ccache ------------------------------------------------------------------------------
#
# Not a micro-optimization here: configuring this project builds the whole of CNA and
# sharp-runtime, and a benchmark is re-configured for several renderer sets.

option(CNA_BENCHMARK_USE_CCACHE "Use ccache for the benchmark, CNA and sharp-runtime" ON)
if(CNA_BENCHMARK_USE_CCACHE)
    find_program(CNA_BENCHMARK_CCACHE ccache)
    if(CNA_BENCHMARK_CCACHE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CNA_BENCHMARK_CCACHE}" CACHE STRING "" FORCE)
        set(CMAKE_C_COMPILER_LAUNCHER "${CNA_BENCHMARK_CCACHE}" CACHE STRING "" FORCE)
    else()
        message(STATUS
            "cna-benchmark: ccache was requested but not found -- building without it. "
            "Install ccache, or configure with -DCNA_BENCHMARK_USE_CCACHE=OFF to silence this.")
    endif()
endif()

# --- Renderer set ------------------------------------------------------------------------
#
# The default set is what a stock Linux/desktop checkout can build with no extra SDKs, and it
# deliberately spans the three interesting classes: a real GPU renderer, a native 2D renderer,
# a CPU rasterizer, and the two no-output renderers that isolate CNA's own CPU-side cost from
# any drawing at all.
#
# Only ONE OpenGL-profile identity may appear: OPENGLES3/OPENGL33/WEBGL2 are
# served by one EasyGL implementation family and cannot be compiled twice into one binary.
# CNA rejects an impossible combination at configure time with a reason -- see
# CNA's renderer-selection CMake modules.

set(CNA_BENCHMARK_RENDERERS "OPENGLES3;SDL_GPU;SOFTWARE;HEADLESS;STUB" CACHE STRING
    "Semicolon-separated CNA renderer identities to compile into cna-benchmark. \
The first entry is the default when --renderer is not given.")

list(LENGTH CNA_BENCHMARK_RENDERERS CNA_BENCHMARK_RENDERER_COUNT)
if(CNA_BENCHMARK_RENDERER_COUNT EQUAL 0)
    message(FATAL_ERROR "CNA_BENCHMARK_RENDERERS must name at least one renderer identity.")
endif()
list(GET CNA_BENCHMARK_RENDERERS 0 CNA_BENCHMARK_DEFAULT_RENDERER)

# CNA's own two selection variables. CNA_GRAPHICS_RENDERER names the default identity;
# CNA_GRAPHICS_RENDERERS is the opt-in list that turns on runtime selection.
set(CNA_GRAPHICS_RENDERER "${CNA_BENCHMARK_DEFAULT_RENDERER}" CACHE STRING
    "CNA default graphics renderer" FORCE)
set(CNA_GRAPHICS_RENDERERS "${CNA_BENCHMARK_RENDERERS}" CACHE STRING
    "CNA renderers compiled in, selectable at runtime" FORCE)

# --- CNA configuration -------------------------------------------------------------------
#
# The benchmark scenes are written against XNA 4.0 plus the always-compiled CNAEXT accessors
# (renderer name, capability profile, Texture2D::CreateFromPixels, the skinned vertex type).
# None of that is behind CNA_CNAEXT, which gates the separate CNA::Graphics engine layer this
# suite does not use -- leaving it off keeps the build smaller and the measured surface the
# stock XNA one every renderer implements.

set(CNA_SHARP_RUNTIME_ROOT "${CNA_BENCHMARK_SHARP_RUNTIME_ROOT}" CACHE PATH
    "Path to the sharp-runtime checkout consumed by this build" FORCE)

# CNA's own component closure plus Text.Json, which this benchmark uses to emit its results.
set(SHARP_RUNTIME_COMPONENTS
    "Core.Base;IO;Collections.Core;Collections.ObjectModel;Runtime;Threading;Text;Globalization;Storage;Security.Cryptography;Text.Json"
    CACHE STRING "Sharp Runtime components required by CNA and the benchmark" FORCE)

set(CNA_CNAEXT OFF CACHE BOOL "CNA extended graphics layer" FORCE)
set(CNA_ENABLE_NET OFF CACHE BOOL "CNA networking" FORCE)
set(CNA_ENABLE_VIDEO "OFF" CACHE STRING "CNA video playback" FORCE)
set(CNA_BUILD_TESTS OFF CACHE BOOL "CNA tests" FORCE)
set(CNA_BUILD_EXAMPLES OFF CACHE BOOL "CNA examples" FORCE)
set(EASYGL_BUILD_TESTS OFF CACHE BOOL "EasyGL tests" FORCE)
set(EASYGL_BUILD_EXAMPLES OFF CACHE BOOL "EasyGL examples" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Transitive dependency tests" FORCE)

add_subdirectory("${CNA_ROOT_DIR}" CNA_BUILD)

if(NOT TARGET CNA)
    message(FATAL_ERROR
        "CNA did not define its umbrella target for CNA_GRAPHICS_RENDERERS="
        "${CNA_BENCHMARK_RENDERERS}.")
endif()

# --- The benchmark -----------------------------------------------------------------------

add_executable(cna-benchmark
    src/main.cpp
    src/CommandLine.cpp
    src/BenchmarkRunner.cpp
    src/Format.cpp
    src/FrameSampler.cpp
    src/Font8x8.cpp
    src/Hud.cpp
    src/OutputProbe.cpp
    src/Procedural.cpp
    src/Report.cpp
    src/ResultWriter.cpp
    src/RunInfo.cpp
    src/SceneRegistry.cpp
    src/Scoring.cpp
    src/scenes/SpritesScene.cpp
    src/scenes/ParticlesScene.cpp
    src/scenes/SkeletalScene.cpp
    src/scenes/TerrainScene.cpp
    src/scenes/LightsScene.cpp
    src/scenes/InstancingScene.cpp
    src/scenes/StreamingScene.cpp
)

target_include_directories(cna-benchmark PRIVATE src)

# Comma-separated, not the semicolon-separated CMake list spelling: a semicolon inside a
# compile definition is a list separator to CMake and reaches the compiler as a broken,
# half-quoted argument.
string(REPLACE ";" "," CNA_BENCHMARK_RENDERERS_TEXT "${CNA_BENCHMARK_RENDERERS}")

target_compile_definitions(cna-benchmark PRIVATE
    CNA_BENCHMARK_VERSION="${PROJECT_VERSION}"
    CNA_BENCHMARK_COMPILED_RENDERERS="${CNA_BENCHMARK_RENDERERS_TEXT}")

# -Wpedantic is deliberately absent: sharp-runtime's System::Decimal builds on the
# compiler's __int128 extension, so every translation unit that reaches Game.hpp -- which is
# all of them -- would report the same upstream warning and bury this project's own.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cna-benchmark PRIVATE -Wall -Wextra)
endif()

target_link_libraries(cna-benchmark PRIVATE CNA)

# The results writer is System.Text.Json from sharp-runtime, not a hand-rolled string builder:
# the schema this suite publishes is meant to be re-read by other tools, and escaping/number
# formatting is exactly where a hand-rolled writer goes wrong.
if(TARGET SharpRuntime::Text.Json)
    target_link_libraries(cna-benchmark PRIVATE SharpRuntime::Text.Json)
elseif(TARGET SHARP_RUNTIME)
    target_link_libraries(cna-benchmark PRIVATE SHARP_RUNTIME)
else()
    message(FATAL_ERROR
        "cna-benchmark: neither SharpRuntime::Text.Json nor the monolithic SHARP_RUNTIME "
        "archive exists. The consumed sharp-runtime checkout at "
        "'${CNA_BENCHMARK_SHARP_RUNTIME_ROOT}' has no System.Text.Json component.")
endif()

find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(cna-benchmark PRIVATE SDL3::SDL3)
if(TARGET SDL3::SDL3main)
    target_link_libraries(cna-benchmark PRIVATE SDL3::SDL3main)
endif()

if(MINGW)
    cna_copy_mingw_cxx_runtime(cna-benchmark)
endif()
if(WIN32)
    cna_copy_sdl_runtime(cna-benchmark)
endif()

# --- Self-checks -------------------------------------------------------------------------
#
# A benchmark that reports a number nobody verified is worse than no benchmark. These are
# cheap, non-timing checks: the ones a wrong build would fail, run in seconds.

option(CNA_BENCHMARK_BUILD_TESTS "Register cna-benchmark's own ctest checks" ON)
if(CNA_BENCHMARK_BUILD_TESTS)
    enable_testing()

    add_test(NAME CnaBenchmarkHelp COMMAND cna-benchmark --help)
    add_test(NAME CnaBenchmarkVersion COMMAND cna-benchmark --version)
    add_test(NAME CnaBenchmarkListScenes COMMAND cna-benchmark --list-scenes)
    add_test(NAME CnaBenchmarkListRenderers COMMAND cna-benchmark --list-renderers)

    # An unknown scene, an unknown renderer and a negative frame count must each fail loudly.
    foreach(_bad_arg "--scenes=no-such-scene" "--renderer=NO_SUCH_RENDERER" "--frames=0")
        string(REGEX REPLACE "[^A-Za-z0-9]" "" _bad_name "${_bad_arg}")
        add_test(NAME "CnaBenchmarkRejects${_bad_name}" COMMAND cna-benchmark ${_bad_arg})
        set_tests_properties("CnaBenchmarkRejects${_bad_name}" PROPERTIES WILL_FAIL TRUE)
    endforeach()

    # An end-to-end run on a renderer that needs no GPU and no window: supported scenes render,
    # unsupported scenes are recorded, and the JSON is written and re-read.
    if("STUB" IN_LIST CNA_BENCHMARK_RENDERERS)
        set(_bench_smoke_renderer STUB)
    elseif("HEADLESS" IN_LIST CNA_BENCHMARK_RENDERERS)
        set(_bench_smoke_renderer HEADLESS)
    else()
        set(_bench_smoke_renderer "")
    endif()
    if(_bench_smoke_renderer)
        add_test(NAME CnaBenchmarkSmokeRun
            COMMAND cna-benchmark
                --renderer=${_bench_smoke_renderer}
                --frames=8 --warmup=2 --workload-scale=0.02 --no-hud
                --json=${CMAKE_CURRENT_BINARY_DIR}/tests/smoke-result.json)
        set_tests_properties(CnaBenchmarkSmokeRun PROPERTIES TIMEOUT 300)

        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME CnaBenchmarkResultSchema
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-result.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/tests/smoke-result.json")
            set_tests_properties(CnaBenchmarkResultSchema PROPERTIES
                DEPENDS CnaBenchmarkSmokeRun)
        endif()
    endif()

    # The STUB smoke run skips the 3D streaming scene. Exercise two consecutive uploads on
    # HEADLESS so a texture left in a sampler slot after SpriteBatch::End is caught here.
    if("HEADLESS" IN_LIST CNA_BENCHMARK_RENDERERS)
        add_test(NAME CnaBenchmarkStreamingUpload
            COMMAND cna-benchmark
                --renderer=HEADLESS --scenes=streaming
                --frames=3 --warmup=1 --workload-scale=0.02 --no-hud --strict --json=)
        set_tests_properties(CnaBenchmarkStreamingUpload PROPERTIES TIMEOUT 30)
    endif()
endif()

message(STATUS "cna-benchmark: renderers = ${CNA_BENCHMARK_RENDERERS} "
               "(default ${CNA_BENCHMARK_DEFAULT_RENDERER})")
