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
    src/scenes/laboratuvar/particles.cu
    src/scenes/laboratuvar/cubes.cu
    src/scenes/laboratuvar/surface.cu
    src/scenes/laboratuvar/weapons.cpp
    src/scenes/laboratuvar/solver_tests.cpp
    src/scenes/laboratuvar/surface_test.cpp
    src/scenes/laboratuvar/weapons_test.cpp
    src/scenes/laboratuvar/lab_geom.cpp
    src/scenes/laboratuvar/lab_kernels.cu
    src/scenes/laboratuvar/lab_world.cpp
    src/scenes/laboratuvar/lab_record.cpp
    src/scenes/laboratuvar/cube_tests.cpp
    src/scenes/laboratuvar/lab_tests.cpp
    src/scenes/laboratuvar/laboratuvar_renderer.cpp
    src/scenes/laboratuvar/laboratuvar_app.cpp
    src/scenes/laboratuvar/laboratuvar_live.cpp
    src/scenes/koy/koy_app.cpp
    src/scenes/koy/koy_live.cpp
    src/scenes/koy/koy_renderer.cpp
    src/scenes/koy/koy_optics.cpp
    src/scenes/koy/koy_world.cpp
    src/scenes/koy/koy_amphora.cpp
    src/scenes/koy/koy_island.cpp
    src/scenes/koy/koy_buoys.cpp
    src/scenes/koy/koy_props.cpp
    src/scenes/koy/koy_reef.cpp
    src/scenes/koy/koy_wreck.cpp
    src/scenes/koy/koy_sdf.cpp
    src/scenes/koy/koy_tests.cpp
    src/scenes/koy/koy_kernels.cu
    src/scenes/heykel/heykel_app.cpp
    src/scenes/heykel/heykel_live.cpp
    src/scenes/heykel/heykel_kernels.cu
    src/scenes/olcek/olcek_world.cpp
    src/scenes/olcek/olcek_inner.cpp
    src/scenes/olcek/olcek_tex.cpp
    src/scenes/olcek/olcek_app.cpp
    src/scenes/olcek/olcek_live.cpp
    src/scenes/olcek/olcek_hiz.cpp
    src/scenes/olcek/olcek_hiz_goz.cpp
    src/scenes/olcek/olcek_hiz_pano.cpp
    src/scenes/olcek/olcek_hiz_yan.cpp
    src/scenes/olcek/olcek_hiz_test.cpp
    src/scenes/olcek/olcek_kernels.cu
    src/scenes/denizalti/dz_gl.cpp
    src/scenes/denizalti/dz_geom.cpp
    src/scenes/denizalti/dz_parts.cpp
    src/scenes/denizalti/dz_world.cpp
    src/scenes/denizalti/dz_room.cpp
    src/scenes/denizalti/dz_equip.cpp
    src/scenes/denizalti/dz_decal.cpp
    src/scenes/denizalti/dz_render.cpp
    src/scenes/denizalti/dz_atlas.cpp
    src/scenes/denizalti/dz_bake.cpp
    src/scenes/denizalti/dz_app.cpp
    src/scenes/denizalti/dz_live.cpp
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
        ${TP}/tinyexr
        ${TP}/gl)
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
        opengl32 advapi32 user32 gdi32 shell32 winmm)
endforeach()

# NVRTC is loaded at run time: keep its DLLs next to the executables (both live in build/).
add_custom_command(TARGET pc_graphics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TP}/cuda/bin/x64/nvrtc64_130_0.dll
        ${TP}/cuda/bin/x64/nvrtc-builtins64_134.dll
        $<TARGET_FILE_DIR:pc_graphics>)
