cmake_minimum_required(VERSION 3.26)
project(pc_graphics LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_ARCHITECTURES 89)
set(TP ${CMAKE_SOURCE_DIR}/third_party)

# Host programs. The OptiX device programs (src/device) are compiled at run time with NVRTC.
# Everything but main.cpp is compiled once (pc_core) and linked into two executables:
#   pc_graphics.exe  scene 1 "Altin saatte cam ve tas" (scene 2 with --scene fener)
#   fener.exe        scene 2 "Fener" straight away (double-click)
#   yikim.exe        scene 3 "Yikim" straight away (double-click)
add_library(pc_core OBJECT
    src/renderer.cpp
    src/scene.cpp
    src/image_io.cpp
    src/gpu_stats.cpp
    src/photons.cu
    src/kernels.cu
    src/denoiser.cpp
    src/fener/fener_app.cpp
    src/fener/fener_renderer.cpp
    src/fener/fener_scene.cpp
    src/yikim/geom.cpp
    src/yikim/phys.cu
    src/yikim/yikim_scenes.cpp
    src/yikim/yikim_tests.cpp
    src/yikim/yikim_app.cpp
    src/yikim/yikim_renderer.cpp
    src/yikim/yikim_filter.cu
    src/yikim/quake.cpp
    src/yikim/building.cpp
    ${TP}/tinyexr/miniz.c)
add_executable(pc_graphics src/main.cpp $<TARGET_OBJECTS:pc_core>)
add_executable(fener src/main.cpp $<TARGET_OBJECTS:pc_core>)
target_compile_definitions(fener PRIVATE PC_DEFAULT_SCENE_FENER)
add_executable(yikim src/main.cpp $<TARGET_OBJECTS:pc_core>)
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
