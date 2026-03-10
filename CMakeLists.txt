# ===========================================================================
# CANmatik — J2534 CAN Bus Scanner & Logger
# ===========================================================================
cmake_minimum_required(VERSION 3.24)

project(canmatik
    VERSION   0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Open-source CAN bus scanner and logger for J2534 Pass-Thru interfaces"
)

# ---------------------------------------------------------------------------
# Global compiler settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 32-bit build for J2534 DLL compatibility
# ---------------------------------------------------------------------------
# Most J2534 drivers ship as 32-bit DLLs; a 64-bit process cannot LoadLibrary
# them. For real-hardware MVP testing, build from the MSYS2 MINGW32 shell
# using the i686 toolchain:
#
#   pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
#   # Open "MSYS2 MinGW 32-bit" shell, then:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# The -m32 flag is NOT used because MSYS2 toolchains are not multilib.
# Instead, the correct architecture is selected by choosing the right
# toolchain (i686 vs x86_64) at configure time.
#
# Detect architecture and warn if building 64-bit:
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "Building 64-bit binary. J2534 adapters typically provide 32-bit DLLs "
        "that cannot be loaded from a 64-bit process. For hardware testing, "
        "rebuild from the MSYS2 MINGW32 shell with the i686 toolchain. "
        "See CMakeLists.txt comments for instructions.")
endif()

# Static linking for portable distribution (no libwinpthread-1.dll etc.)
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

# Export compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# FetchContent — external dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# CLI11 — command-line parser
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
)

# nlohmann/json — JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# Catch2 — testing framework
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)

FetchContent_MakeAvailable(CLI11 nlohmann_json Catch2)

# ---------------------------------------------------------------------------
# Third-party vendored libraries (TinyLog, future ImGui)
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Canmatik core library (shared by CLI and future GUI)
# ---------------------------------------------------------------------------
add_library(canmatik_core STATIC
    # Core value types
    src/core/can_frame.cpp
    src/core/session_status.cpp
    src/core/filter.cpp
    src/core/label_store.cpp
    # Transport interfaces (headers only, but listed for IDE indexing)
    # Configuration
    src/config/config.cpp
)

target_include_directories(canmatik_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_core PUBLIC
    nlohmann_json::nlohmann_json
    tinylog
)

target_compile_features(canmatik_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Platform library (Windows J2534 implementation)
# ---------------------------------------------------------------------------
add_library(canmatik_platform STATIC
    src/platform/win32/j2534_dll_loader.cpp
    src/platform/win32/registry_scanner.cpp
    src/platform/win32/j2534_provider.cpp
    src/platform/win32/j2534_channel.cpp
)

target_include_directories(canmatik_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_platform PUBLIC
    canmatik_core
)

# ---------------------------------------------------------------------------
# Mock library (test/demo backend)
# ---------------------------------------------------------------------------
add_library(canmatik_mock STATIC
    src/mock/mock_provider.cpp
    src/mock/mock_channel.cpp
)

target_include_directories(canmatik_mock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_mock PUBLIC
    canmatik_core
)

# ---------------------------------------------------------------------------
# Services library
# ---------------------------------------------------------------------------
add_library(canmatik_services STATIC
    src/services/session_service.cpp
    src/services/capture_service.cpp
    src/services/record_service.cpp
    src/services/replay_service.cpp
)

target_include_directories(canmatik_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_services PUBLIC
    canmatik_core
)

# ---------------------------------------------------------------------------
# Logging library (ASC/JSONL writers and readers)
# ---------------------------------------------------------------------------
add_library(canmatik_logging STATIC
    src/logging/asc_writer.cpp
    src/logging/jsonl_writer.cpp
    src/logging/asc_reader.cpp
    src/logging/jsonl_reader.cpp
)

target_include_directories(canmatik_logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_logging PUBLIC
    canmatik_core
)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(canmatik
    src/cli/main.cpp
    src/cli/cli_app.cpp
    src/cli/cmd_scan.cpp
    src/cli/cmd_monitor.cpp
    src/cli/cmd_record.cpp
    src/cli/cmd_replay.cpp
    src/cli/cmd_status.cpp
    src/cli/cmd_demo.cpp
    src/cli/cmd_label.cpp
    src/cli/canmatik.rc
)

target_include_directories(canmatik PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik PRIVATE
    canmatik_core
    canmatik_platform
    canmatik_mock
    canmatik_services
    canmatik_logging
    CLI11::CLI11
    tinylog
)

# ---------------------------------------------------------------------------
# Fake J2534 DLL (sniff tool behavior / future ECU simulation)
# ---------------------------------------------------------------------------
add_library(fake_j2534 SHARED
    src/fake_j2534/fake_j2534.cpp
    src/fake_j2534/fake_j2534.def
)

target_compile_definitions(fake_j2534 PRIVATE _WIN32_WINNT=0x0600)
set_target_properties(fake_j2534 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fake_j2534"
)

# Resolve __stdcall decorated names to .def undecorated names
if(MINGW)
    target_link_options(fake_j2534 PRIVATE "-Wl,--enable-stdcall-fixup")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
