cmake_minimum_required(VERSION 3.20)
project(RustClient)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Injector EXE ─────────────────────────────────────────
add_executable(RustInjector
    src/injector.cpp
)

target_compile_definitions(RustInjector PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN
)

target_link_libraries(RustInjector PRIVATE
    Advapi32
)

set_target_properties(RustInjector PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ── Payload DLL ───────────────────────────────────────────
add_library(RustDLL SHARED
    dll/src/dllmain.cpp
    dll/src/hook.cpp
    dll/src/overlay.cpp
    dll/src/fps.cpp
    dll/src/keystrokes.cpp
    dll/src/menu.cpp
)

target_include_directories(RustDLL PRIVATE
    dll/src
    dll/src/vendor
)

target_compile_definitions(RustDLL PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

target_link_libraries(RustDLL PRIVATE
    d3d11 d3d12 dxgi d2d1 dwrite
    dwmapi Advapi32
)

set_target_properties(RustDLL PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
