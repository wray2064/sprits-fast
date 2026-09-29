@echo off
setlocal
rem A build to hand to someone: optimised, no console window, and the files
rem it needs beside it, in dist\SpritsFast -- copy that folder anywhere and
rem run sprits_fast.exe. It needs no network and installs nothing.
set VSLANG=1033
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-release (
  "%CMAKE%" -S . -B build-release -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=Release -DFAST_BUILD_GUI=ON -DFAST_BUILD_TESTS=OFF || exit /b 1
)
"%CMAKE%" --build build-release || exit /b 1

if exist dist\SpritsFast rmdir /s /q dist\SpritsFast
mkdir dist\SpritsFast || exit /b 1
copy /y build-release\sprits_fast.exe dist\SpritsFast\ >nul || exit /b 1
xcopy /e /i /q /y assets\lang dist\SpritsFast\lang >nul || exit /b 1
rem The C++ runtime beside the program, so a machine without the Visual C++
rem redistributable still runs it.
for %%d in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll) do (
  if exist "%VCToolsRedistDir%x64\Microsoft.VC145.CRT\%%d" (
    copy /y "%VCToolsRedistDir%x64\Microsoft.VC145.CRT\%%d" dist\SpritsFast\ >nul
  ) else if exist "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\%%d" (
    copy /y "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\%%d" dist\SpritsFast\ >nul
  )
)
echo Built dist\SpritsFast
