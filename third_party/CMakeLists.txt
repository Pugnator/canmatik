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

# ImGui — now built via FetchContent in root CMakeLists.txt
