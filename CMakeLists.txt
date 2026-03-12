# ===========================================================================
# CANmatik — J2534 CAN Bus Scanner & Logger
# ===========================================================================
cmake_minimum_required(VERSION 3.24)

# Allow older cmake_minimum_required in FetchContent dependencies (CMake 4.0 compat)
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# 32-bit build for J2534 DLL compatibility
# ---------------------------------------------------------------------------
# Most J2534 drivers ship as 32-bit DLLs; a 64-bit process cannot LoadLibrary
# them.
#
# MSVC:   The platform is forced to Win32 automatically below.
#         Override with: cmake -B build -A x64  (if you really need 64-bit)
#
# MINGW:  Use the MSYS2 MINGW32 shell with the i686 toolchain:
#           pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
#           cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#           cmake --build build
#         The -m32 flag is NOT used because MSYS2 toolchains are not multilib.
# ---------------------------------------------------------------------------

# For MSVC generators, default to 32-bit (Win32) unless the user explicitly
# requested a different platform via -A on the command line.
if(NOT CMAKE_GENERATOR_PLATFORM AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "Target platform" FORCE)
    message(STATUS "MSVC detected ÔÇö defaulting to 32-bit (Win32) for J2534 compatibility.")
endif()

project(canmatik
    VERSION   0.2.0
    LANGUAGES C CXX
    DESCRIPTION "Open-source CAN bus scanner, recorder and anlyzer for J2534 Pass-Thru interfaces"
)

# ---------------------------------------------------------------------------
# Global compiler settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warn if we ended up with a 64-bit build anyway (e.g. MinGW x86_64)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "Building 64-bit binary. J2534 adapters typically provide 32-bit DLLs "
        "that cannot be loaded from a 64-bit process. For hardware testing, "
        "rebuild with a 32-bit toolchain. See CMakeLists.txt comments.")
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

# yaml-cpp — YAML parser (for OBD-II config)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    OVERRIDE_FIND_PACKAGE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(CLI11 nlohmann_json Catch2 yaml-cpp)

# ---------------------------------------------------------------------------
# Third-party vendored libraries (TinyLog, ImGui)
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
# OBD-II diagnostics library (J1979 modes, PID decoding, YAML config)
# ---------------------------------------------------------------------------
add_library(canmatik_obd STATIC
    src/obd/interval_spec.cpp
    src/obd/obd_config.cpp
    src/obd/pid_table.cpp
    src/obd/pid_decoder.cpp
    src/obd/dtc_decoder.cpp
    src/obd/obd_response.cpp
    src/obd/obd_session.cpp
    src/obd/query_scheduler.cpp
)

target_include_directories(canmatik_obd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_obd PUBLIC
    canmatik_core
    yaml-cpp
)

target_compile_features(canmatik_obd PUBLIC cxx_std_20)

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
    src/cli/cmd_obd.cpp
    src/cli/cmd_sniff.cpp
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
    canmatik_obd
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
# GUI executable (ImGui + Win32/OpenGL3)
# ---------------------------------------------------------------------------
add_executable(canmatik_gui WIN32
    src/gui/gui_main.cpp
    src/gui/gui_app.cpp
    src/gui/canmatik_gui.rc
    src/gui/gui_settings.cpp
    src/gui/frame_collector.cpp
    src/gui/controllers/capture_controller.cpp
    src/gui/controllers/replay_controller.cpp
    src/gui/controllers/obd_controller.cpp
    src/gui/panels/can_messages_panel.cpp
    src/gui/panels/obd_data_panel.cpp
    src/gui/panels/dtc_panel.cpp
    src/gui/panels/settings_panel.cpp
    src/gui/panels/watchdog_panel.cpp
    src/gui/panels/status_bar.cpp
    src/gui/panels/menu_bar.cpp
    src/gui/widgets/playback_toolbar.cpp
)

target_include_directories(canmatik_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(canmatik_gui PRIVATE
    canmatik_core
    canmatik_platform
    canmatik_mock
    canmatik_services
    canmatik_logging
    canmatik_obd
    imgui
    tinylog
    nlohmann_json::nlohmann_json
    opengl32
    comdlg32
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
