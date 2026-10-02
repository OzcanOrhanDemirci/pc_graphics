cmake_minimum_required(VERSION 3.26)
project(pc_graphics LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_ARCHITECTURES 89)
set(TP ${CMAKE_SOURCE_DIR}/third_party)

# One application, pc_graphics.exe: the gallery of scenes and each scene's command-line tools.
#   src/app      the application (window, gallery, loading, the interface over a scene, the scene registry)
#   src/ui       the interface kit (shapes, images, text in IBM Plex)
#   src/core     what the scenes share (settings, images, GPU readings, bloom)
#   src/scenes   one folder per scene: host code, and device/ with its OptiX programs (compiled at run time by NVRTC)
add_library(pc_core OBJECT
    src/app/app.cpp
    src/app/lang.cpp
    src/app/overlay.cpp
    src/app/registry.cpp
    src/ui/ui.cpp
    src/core/config.cpp
    src/core/image_io.cpp
    src/core/gpu_stats.cpp
    src/core/bloom.cu
    src/scenes/altinsaat/altinsaat_app.cpp
    src/scenes/altinsaat/altinsaat_live.cpp
    src/scenes/altinsaat/altinsaat_renderer.cpp
    src/scenes/altinsaat/altinsaat_world.cpp
    src/scenes/altinsaat/photons.cu
    src/scenes/altinsaat/kernels.cu
    src/scenes/altinsaat/denoiser.cpp
    src/scenes/fener/fener_app.cpp
    src/scenes/fener/fener_live.cpp
    src/scenes/fener/fener_renderer.cpp
    src/scenes/fener/fener_scene.cpp
    src/scenes/yikim/yikim_app.cpp
    src/scenes/yikim/yikim_live.cpp
    src/scenes/yikim/yikim_renderer.cpp
    src/scenes/yikim/yikim_filter.cu
    src/scenes/yikim/yikim_scenes.cpp
    src/scenes/yikim/yikim_tests.cpp
    src/scenes/yikim/building.cpp
    src/scenes/yikim/geom.cpp
    src/scenes/yikim/phys.cu
    src/scenes/yikim/quake.cpp
    src/scenes/tanecik/body.cpp
    src/scenes/tanecik/body_graph.cpp
    src/scenes/tanecik/tanecik_kernels.cu
    src/scenes/tanecik/skeleton_sim.cu
    src/scenes/tanecik/balance.cpp
    src/scenes/tanecik/tanecik_skeleton_test.cpp
    src/scenes/tanecik/tanecik_app.cpp
    src/scenes/tanecik/tanecik_live.cpp
    src/scenes/tanecik/tanecik_renderer.cpp
    src/scenes/tanecik/surface.cu
    src/scenes/tanecik/tanecik_surface_test.cpp
    ${TP}/tinyexr/miniz.c)
add_executable(pc_graphics src/app/main.cpp $<TARGET_OBJECTS:pc_core>)

foreach(t pc_core pc_graphics)
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

foreach(t pc_graphics)
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
