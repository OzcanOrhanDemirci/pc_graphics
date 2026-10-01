cmake_minimum_required(VERSION 3.26)
project(pc_graphics LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_ARCHITECTURES 89)
set(TP ${CMAKE_SOURCE_DIR}/third_party)

# Host programs. The OptiX device programs (src/scenes/*/device) are compiled at run time with NVRTC.
# Everything but the entry point is compiled once (pc_core): the shared core, the interface kit and the scenes.
add_library(pc_core OBJECT
    src/core/config.cpp
    src/core/image_io.cpp
    src/core/gpu_stats.cpp
    src/core/bloom.cu
    src/scenes/altinsaat/altinsaat_renderer.cpp
    src/scenes/altinsaat/altinsaat_world.cpp
    src/scenes/altinsaat/photons.cu
    src/scenes/altinsaat/kernels.cu
    src/scenes/altinsaat/denoiser.cpp
    src/scenes/fener/fener_app.cpp
    src/scenes/fener/fener_renderer.cpp
    src/scenes/fener/fener_scene.cpp
    src/scenes/yikim/geom.cpp
    src/scenes/yikim/phys.cu
    src/scenes/yikim/yikim_scenes.cpp
    src/scenes/yikim/yikim_tests.cpp
    src/scenes/yikim/yikim_app.cpp
    src/scenes/yikim/yikim_renderer.cpp
    src/scenes/yikim/yikim_filter.cu
    src/scenes/yikim/quake.cpp
    src/scenes/yikim/building.cpp
    ${TP}/tinyexr/miniz.c)
add_executable(pc_graphics src/scenes/altinsaat/altinsaat_app.cpp $<TARGET_OBJECTS:pc_core>)
add_executable(fener src/scenes/altinsaat/altinsaat_app.cpp $<TARGET_OBJECTS:pc_core>)
target_compile_definitions(fener PRIVATE PC_DEFAULT_SCENE_FENER)
add_executable(yikim src/scenes/altinsaat/altinsaat_app.cpp $<TARGET_OBJECTS:pc_core>)
target_compile_definitions(yikim PRIVATE PC_DEFAULT_SCENE_YIKIM)

foreach(t pc_core pc_graphics fener yikim)
    target_include_directories(${t} PRIVATE
        src
        ${TP}/cuda/include
        ${TP}/cuda/include/cccl
        ${TP}/optix/include
        ${TP}/glfw/include
        ${TP}/stb
        ${TP}/tinyexr)
    target_compile_definitions(${t} PRIVATE
        PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /O2 /utf-8 /wd4244 /wd4267 /wd4305>)
        # CUB (CCCL) needs MSVC's standard-conforming preprocessor
        target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor -Xcompiler=/utf-8>)
    endif()
endforeach()

foreach(t pc_graphics fener yikim)
    target_link_libraries(${t} PRIVATE
        ${TP}/cuda/lib/x64/cudart_static.lib
        ${TP}/cuda/lib/x64/cuda.lib
        ${TP}/cuda/lib/x64/nvrtc.lib
        ${TP}/cuda/lib/x64/nvml.lib
        ${TP}/glfw/lib-vc2022/glfw3.lib
        opengl32 advapi32 user32 gdi32 shell32)
endforeach()

# NVRTC is loaded at run time: keep its DLLs next to the executables (both live in build/).
add_custom_command(TARGET pc_graphics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TP}/cuda/bin/x64/nvrtc64_130_0.dll
        ${TP}/cuda/bin/x64/nvrtc-builtins64_134.dll
        $<TARGET_FILE_DIR:pc_graphics>)
