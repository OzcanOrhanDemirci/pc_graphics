@echo off
rem Configures (first time) and builds stress.exe into build\. MSVC from VS 2026, CMake + Ninja from VS,
rem CUDA from third_party\cuda.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
set VSCM=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake
set PATH=%VSCM%\CMake\bin;%VSCM%\Ninja;%~dp0third_party\cuda\bin;%PATH%
cd /d %~dp0
if not exist build\build.ninja (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_COMPILER="%~dp0third_party\cuda\bin\nvcc.exe" || exit /b 1
)
cmake --build build || exit /b 1
