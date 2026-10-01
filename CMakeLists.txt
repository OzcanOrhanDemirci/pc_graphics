cmake_minimum_required(VERSION 3.26)
project(pc_graphics LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_ARCHITECTURES 89)
set(TP ${CMAKE_SOURCE_DIR}/third_party)

# Host program. The OptiX device programs (src/device) are compiled at run time with NVRTC.
add_executable(pc_graphics
    src/main.cpp
    src/renderer.cpp
    src/scene.cpp
    src/image_io.cpp
    src/gpu_stats.cpp
    src/photons.cu
    src/kernels.cu
    src/denoiser.cpp
    ${TP}/tinyexr/miniz.c)

target_include_directories(pc_graphics PRIVATE
    src
    ${TP}/cuda/include
    ${TP}/cuda/include/cccl
    ${TP}/optix/include
    ${TP}/glfw/include
    ${TP}/stb
    ${TP}/tinyexr)

target_compile_definitions(pc_graphics PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(pc_graphics PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /O2 /utf-8 /wd4244 /wd4267 /wd4305>)
    # CUB (CCCL) needs MSVC's standard-conforming preprocessor
    target_compile_options(pc_graphics PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor -Xcompiler=/utf-8>)
endif()

target_link_libraries(pc_graphics PRIVATE
    ${TP}/cuda/lib/x64/cudart_static.lib
    ${TP}/cuda/lib/x64/cuda.lib
    ${TP}/cuda/lib/x64/nvrtc.lib
    ${TP}/cuda/lib/x64/nvml.lib
    ${TP}/glfw/lib-vc2022/glfw3.lib
    opengl32 advapi32 user32 gdi32 shell32)

# NVRTC is loaded at run time: keep its DLLs next to the executable.
add_custom_command(TARGET pc_graphics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TP}/cuda/bin/x64/nvrtc64_130_0.dll
        ${TP}/cuda/bin/x64/nvrtc-builtins64_134.dll
        $<TARGET_FILE_DIR:pc_graphics>)
