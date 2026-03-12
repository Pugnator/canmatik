# ===========================================================================
# third_party/CMakeLists.txt — Build vendored dependencies as STATIC libraries
# ===========================================================================

# ---------------------------------------------------------------------------
# zstd — built as static only (no programs, no tests)
# TinyLog's own CMakeLists.txt hardcodes SHARED, so we bypass it entirely and
# compile TinyLog's source directly as a static library.
# ---------------------------------------------------------------------------
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
add_subdirectory(tinylog/vendor/zstd/build/cmake)

# ---------------------------------------------------------------------------
# TinyLog — compiled from source as STATIC (bypasses TinyLog's SHARED default)
# Only log.cc is needed; dllmain.cc is DLL-specific boilerplate.
# ---------------------------------------------------------------------------
add_library(tinylog STATIC
    tinylog/log.cc
)

target_include_directories(tinylog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tinylog
)

target_include_directories(tinylog PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tinylog/vendor/zstd/lib
)

target_link_libraries(tinylog PRIVATE libzstd_static)

# TinyLog requires C++20 for std::format
target_compile_features(tinylog PUBLIC cxx_std_20)

# Suppress warnings from third-party code
target_compile_options(tinylog PRIVATE -w)

# ---------------------------------------------------------------------------
# ImGui — built from submodule as STATIC (Win32 + OpenGL3 backend)
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_win32.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC opengl32 gdi32 dwmapi)
target_compile_options(imgui PRIVATE -w)
