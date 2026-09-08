@echo off
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-gui (
  "%CMAKE%" -S . -B build-gui -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFAST_BUILD_GUI=ON || exit /b 1
)
"%CMAKE%" --build build-gui || exit /b 1
