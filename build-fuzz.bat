@echo off
setlocal
rem The fuzz build: AddressSanitizer on everything (Fast, the engine, the tests)
rem and the coverage-guided libFuzzer entry point (tests\fuzz\libfuzzer_main.cpp).
rem It lives in build-fuzz\, apart from the ordinary build, and is run by
rem tools\fuzz.ps1. See docs\fuzzing.md.
set VSLANG=1033
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-fuzz (
  "%CMAKE%" -S . -B build-fuzz -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFAST_FUZZ=ON -DFAST_BUILD_GUI=OFF ^
    "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /fsanitize=address" ^
    "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /fsanitize=address" || exit /b 1
)
"%CMAKE%" --build build-fuzz --target fast_libfuzzer fast_fuzz_driver || exit /b 1
