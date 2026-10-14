@echo off
:: Rust Client — Build Script
:: Run this from the injector/ folder in a Developer Command Prompt for VS

echo [Rust] Building injector + DLL...

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [Rust] CMake configure failed.
    pause
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo [Rust] Build failed.
    pause
    exit /b 1
)

echo.
echo [Rust] Build complete.
echo Output: build\bin\Release\RustInjector.exe
echo         build\bin\Release\RustDLL.dll
echo.
echo Copy both files to the launcher's resources folder.
pause
