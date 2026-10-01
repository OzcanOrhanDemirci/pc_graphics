@echo off
rem Builds and runs the smoke test: MSVC (VS 2026) as host compiler, the project's own CUDA 13.4.2.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set ROOT=%~dp0..\..
set CUDA=%ROOT%\third_party\cuda
set OPTIX=%ROOT%\third_party\optix\include
set PATH=%CUDA%\bin;%CUDA%\bin\x64;%PATH%
cd /d %~dp0
if not exist out mkdir out
nvcc --version | findstr release || exit /b 1
nvcc -optix-ir -arch=sm_89 -I"%OPTIX%" -o out\smoke_device.optixir smoke_device.cu || exit /b 1
nvcc -O2 -arch=sm_89 -std=c++20 -I"%OPTIX%" -o out\smoke.exe smoke.cu -lcuda -lnvml -ladvapi32 || exit /b 1
copy /y "%CUDA%\bin\x64\cudart64_13.dll" out\ >nul 2>&1
copy /y "%CUDA%\bin\cudart64_13.dll" out\ >nul 2>&1
out\smoke.exe out\smoke_device.optixir
