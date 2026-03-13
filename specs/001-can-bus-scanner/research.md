# Technology Research: J2534 CAN Bus Scanner & Logger

**Feature Branch**: `001-can-bus-scanner`  
**Created**: 2026-03-08  
**Status**: Complete  
**Input**: Research topics from planning phase

---

## Topic 1: C++ Testing Framework for MinGW + CMake

### Decision

**Catch2 v3** — header-only option available, excellent CMake integration, zero friction with MinGW.

### Rationale

| Criterion | GoogleTest | Catch2 v3 | doctest |
|---|---|---|---|
| **CMake integration** | Excellent (`FetchContent`, `find_package`, `GTest::GTest` target) | Excellent (`FetchContent`, `find_package`, `Catch2::Catch2` target, ships `Catch2WithMain`) | Good (`FetchContent`, `find_package`) |
| **MinGW compatibility** | Works but has historical friction: requires `winpthreads`, occasional linking issues with MinGW-w64 builds, `gtest_force_shared_crt` hacks for MSVC CRT mismatches | Clean MinGW builds out of the box; no CRT or threading library complications | Clean MinGW builds; no known issues |
| **Header-only option** | No — requires building `libgtest.a` / `libgtest_main.a` | Yes — `catch_amalgamated.hpp` single-header mode available in v3; also supports compiled library mode for faster rebuilds | Yes — single-header by design, ~4500 lines |
| **Community adoption** | Largest (Google, Chromium, LLVM, thousands of OSS projects) | Very large (Boost-level recognition, widely used in C++ OSS) | Smaller but active; explicitly positions itself as Catch2 alternative |
| **CI friendliness** | Built-in JUnit XML reporter; `ctest` integration | Built-in JUnit XML, TAP, compact reporters; first-class `ctest` integration via `catch_discover_tests()` | JUnit XML reporter; `ctest` integration |
| **C++17 support** | Full | Full | Full |
| **Build speed** | Moderate (compiled library) | Fast in single-header mode; moderate in compiled mode | Fast (lightweight header) |
| **BDD-style tests** | No native support | `SCENARIO` / `GIVEN` / `WHEN` / `THEN` macros map directly to spec acceptance scenarios | `SCENARIO` / `GIVEN` / `WHEN` / `THEN` supported |

**Why Catch2 over GoogleTest**: GoogleTest's MinGW friction (threading library, CRT flags) is unnecessary overhead for a project that already has enough platform complexity with J2534 + ImGui. Catch2's `catch_discover_tests()` CMake function auto-registers tests with CTest, and its BDD macros map directly to the acceptance scenarios in the spec (e.g., "Given an active CAN session, When the user applies a filter…").

**Why Catch2 over doctest**: doctest is leaner, but Catch2 has wider adoption, more mature reporters, better documentation, and the `Catch2WithMain` CMake target eliminates boilerplate. The build speed advantage of doctest is marginal in a project of this size.

### Alternatives

- **GoogleTest**: Choose if team already knows it or if project later needs mocking (GoogleMock). Can be added alongside Catch2 if needed.
- **doctest**: Choose if compile time becomes a bottleneck and Catch2's single-header mode is too slow. Near-identical syntax makes migration trivial.

### Integration Pattern

```cmake
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(tests test_main.cpp test_j2534.cpp test_can_frame.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tests)
```

---

## Topic 2: J2534 API on Windows — DLL Loading Best Practices

### Decision

**Runtime dynamic loading via `LoadLibrary` + `GetProcAddress`** with provider discovery from the Windows Registry. Use a custom minimal J2534 header derived from the SAE specification, with explicit `__stdcall` calling convention and `#pragma pack(1)` struct packing.

### Discovery — How Applications Find J2534 Providers

J2534 providers register themselves under:

```
HKLM\SOFTWARE\PassThruSupport.04.04\<ProviderName>
```

On 64-bit Windows, 32-bit providers (which is the vast majority) register under the WOW6432Node redirected path:

```
HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\<ProviderName>
```

Each provider subkey contains at minimum:

| Value Name | Type | Description |
|---|---|---|
| `Name` | `REG_SZ` | Human-readable provider name (e.g., "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM") |
| `FunctionLibrary` | `REG_SZ` | Full path to the J2534 DLL (e.g., `C:\Program Files (x86)\OpenPort\op20pt32.dll`) |
| `Vendor` | `REG_SZ` | Vendor name |
| `CAN` | `REG_DWORD` | `1` if CAN protocol supported |
| `ISO15765` | `REG_DWORD` | `1` if ISO 15765 (CAN + ISO-TP) supported |

**Discovery algorithm**:
1. Open `HKLM\SOFTWARE\PassThruSupport.04.04` (with `KEY_WOW64_32KEY` flag if building 32-bit).
2. Enumerate subkeys.
3. For each subkey, read `Name`, `FunctionLibrary`, and protocol support flags.
4. Filter for providers that advertise `CAN = 1`.
5. Present list to user.

**Important**: The application should be built as **32-bit** (`-m32` in MinGW) because nearly all J2534 DLLs are 32-bit. A 64-bit process cannot load a 32-bit DLL. Some newer adapters (e.g., Drew Technologies, Bosch) offer 64-bit DLLs, but 32-bit is the safe universal choice for MVP.

### DLL Loading — Runtime Binding

J2534 DLLs are **never** linked at compile time. The standard pattern:

```cpp
HMODULE hDll = LoadLibraryA(dllPath.c_str());
if (!hDll) { /* handle error with GetLastError() */ }

// Resolve each function
using PassThruOpen_t = long (__stdcall *)(void*, unsigned long*);
auto pfnPassThruOpen = reinterpret_cast<PassThruOpen_t>(
    GetProcAddress(hDll, "PassThruOpen")
);
if (!pfnPassThruOpen) { /* handle missing export */ }
```

**Wrapper class pattern**: Encapsulate the DLL handle and function pointers in a RAII class that loads in the constructor and calls `FreeLibrary` in the destructor.

### Key J2534 API Functions for CAN Sniffing

| Function | Purpose | Notes |
|---|---|---|
| `PassThruOpen` | Open device connection | Takes device name (or `NULL` for default), returns device ID |
| `PassThruConnect` | Open a protocol channel | Protocol = `CAN` (0x05), baudrate, flags; returns channel ID |
| `PassThruStartMsgFilter` | Set hardware-level CAN filter | `PASS_FILTER` type with mask/pattern for pass-all or selective |
| `PassThruReadMsgs` | Read received CAN frames | Blocking with timeout; fills array of `PASSTHRU_MSG` structs |
| `PassThruDisconnect` | Close protocol channel | Takes channel ID |
| `PassThruClose` | Close device connection | Takes device ID |
| `PassThruGetLastError` | Get error description string | Returns human-readable error text |

**Pass-all filter** for sniffing (required — without a filter, no messages are received):

```cpp
PASSTHRU_MSG maskMsg = {}, patternMsg = {};
maskMsg.ProtocolID = CAN;
maskMsg.DataSize = 4;  // 4 bytes for CAN ID
// mask and pattern all zeros = pass everything
unsigned long filterId;
PassThruStartMsgFilter(channelId, PASS_FILTER, &maskMsg, &patternMsg, nullptr, &filterId);
```

### MinGW ↔ MSVC DLL Pitfalls

| Issue | Problem | Solution |
|---|---|---|
| **Calling convention** | J2534 functions use `__stdcall` (WINAPI). MinGW defaults to `__cdecl`. Wrong convention = stack corruption / crash. | Explicitly declare all function pointer types with `__stdcall`. |
| **Struct packing** | `PASSTHRU_MSG` is defined with specific packing in the J2534 spec. MSVC and MinGW may pad differently by default. | Use `#pragma pack(push, 1)` around struct definitions, or `__attribute__((packed))` for GCC. Verify `sizeof(PASSTHRU_MSG)` matches expected size (usually 4152 bytes). |
| **Name decoration** | `__stdcall` functions in 32-bit DLLs may be decorated (`_PassThruOpen@8`). Most J2534 DLLs export undecorated names via `.def` files. | Use `GetProcAddress` with undecorated names first; if that fails, try `_Name@N` pattern. In practice, all production J2534 DLLs export plain names. |
| **Data types** | J2534 spec uses `unsigned long` (4 bytes on both MSVC and MinGW 32-bit). On MinGW 64-bit, `unsigned long` may be 8 bytes (GCC follows LP64 on some configs). | Use `uint32_t` or equivalent fixed-width types in the wrapper, and ensure 32-bit build. |
| **CRT mismatch** | The DLL uses MSVC CRT; the app uses MinGW's CRT. | Not an issue since J2534 API uses C types only (no `std::string`, no CRT objects cross the boundary). The API boundary is pure C. |

### Open-Source J2534 Headers and Wrappers

| Resource | Description | Status |
|---|---|---|
| **pandaJ2534DLL** (comma.ai) | Open-source J2534 implementation for the Panda OBD adapter. Contains clean J2534 header definitions. | MIT license; good reference for struct definitions |
| **j2534-rs** (Rust) | Rust bindings for J2534. Contains well-documented API constant definitions. | Reference only (Rust, not C++) |
| **OpenStar J2534** | Community J2534 documentation with header files. | Useful for validation |
| **SAE J2534-1** | The official specification. Not freely available, but the API surface is well-documented in open implementations. | Definitive reference |

**Recommendation**: Write a minimal custom header (`j2534_defs.h`) containing only the types, constants, and function signatures needed for CAN sniffing. This avoids licensing issues with copying vendor headers and ensures correct `__stdcall` + packing for MinGW. Validate struct sizes with `static_assert`.

### Alternatives

- **Link-time binding** (import library): Not viable — no single `.a` / `.lib` exists for all providers; each provider has its own DLL.
- **COM / OLE Automation**: Some J2534 implementations expose COM interfaces, but this is non-standard and not portable across providers.
- **Build as 64-bit + use out-of-process proxy**: Possible but extreme complexity for MVP.

---

## Topic 3: ImGui Integration for a CMake/MinGW Desktop App

### Decision

**Win32 + OpenGL3 backend**, integrated via **vendored copy** (git submodule or direct copy of ImGui source files).

### Backend Comparison

| Backend Combo | MinGW Compatibility | Dependencies | Build Complexity |
|---|---|---|---|
| **Win32 + DirectX11** | Problematic — MinGW ships incomplete DirectX headers, `d3d11.h` / `d3dcompiler.h` may be missing or outdated. Requires Windows SDK headers. | `d3d11.lib`, `d3dcompiler_47.dll` | High for MinGW |
| **Win32 + OpenGL3** | Excellent — OpenGL headers are standard in MinGW, `opengl32.lib` ships with MinGW-w64. Win32 window management uses only standard Windows API. | `opengl32`, `gdi32` (both in MinGW) | Low |
| **SDL2 + OpenGL3** | Good — SDL2 works with MinGW. Adds SDL2 as dependency but provides better cross-platform potential. | SDL2 library | Medium (must build or find MinGW SDL2 binaries) |
| **GLFW + OpenGL3** | Excellent — GLFW builds cleanly with MinGW/CMake. Popular in ImGui examples. | GLFW library | Medium |

**Why Win32 + OpenGL3**: Zero external dependencies beyond what MinGW-w64 already provides. The Win32 platform backend (`imgui_impl_win32.cpp`) uses standard Win32 API calls for window creation, message handling, and input — no SDK-specific headers needed. The OpenGL3 renderer backend (`imgui_impl_opengl3.cpp`) uses `opengl32.dll` which is present on every Windows installation. This is the lightest-weight option for a Windows-only tool.

**Why not DirectX11**: MinGW's DirectX support is incomplete. Getting `d3d11.h`, `dxgi.h`, and the shader compiler to work requires either installing the Windows SDK (which is MSVC-centric) or using community-maintained MinGW DirectX headers. This adds friction with no real benefit for a tool UI.

**Why not SDL2/GLFW**: Adds an external dependency that must be built or vendored. For an MVP that targets Windows only, this is extra weight. However, **SDL2 or GLFW should be reconsidered** if cross-platform support becomes a goal.

### CMake Integration Approach

| Method | Pros | Cons |
|---|---|---|
| **FetchContent** | Auto-downloads; version-pinned | ImGui has no official CMakeLists.txt; requires writing one or using a wrapper repo |
| **Git submodule** | Exact version control; works offline | Manual update; adds submodule complexity |
| **Vendored copy** | Simple; no git submodule; full control | Manual updates; larger repo |

**Recommendation**: **Git submodule** pointing to the official `ocornut/imgui` repository at a tagged release. Write a small `CMakeLists.txt` in the project that compiles the needed ImGui source files:

```cmake
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC opengl32 gdi32 dwmapi)
```

### MinGW-Specific Build Issues

| Issue | Solution |
|---|---|
| **`ImGui_ImplWin32_WndProcHandler`** linking | Ensure `imgui_impl_win32.cpp` is compiled in the same translation unit context; declare `extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(...)` in your window code. |
| **OpenGL loader** | ImGui's OpenGL3 backend can optionally use a loader (glad, glew). For basic usage, ImGui's built-in loader suffices — define `IMGUI_IMPL_OPENGL_LOADER_CUSTOM` or let it auto-detect. With MinGW + basic WGL, no extra loader is needed for OpenGL ≤ 3.0. For OpenGL 3.1+, bundle **glad** (header-only generated file). |
| **`WGL` context creation** | Win32 backend handles this. Ensure linking against `opengl32` and `gdi32`. |
| **Unicode / wide strings** | Define `UNICODE` and `_UNICODE` if using wide Win32 APIs. ImGui internally uses UTF-8; the Win32 backend handles conversion. |
| **`dwmapi.lib`** | ImGui 1.89+ platform backend may reference `DwmExtendFrameIntoClientArea`. Link against `dwmapi`. MinGW-w64 provides this. |

### Alternatives

- **Win32 + DirectX11**: Revisit if OpenGL performance is insufficient (unlikely for a diagnostic tool UI).
- **GLFW + OpenGL3**: Revisit if cross-platform becomes a requirement.
- **SDL2 + OpenGL3**: Best option for cross-platform support including Linux.
- **No GUI (MVP is CLI-only per spec)**: The spec explicitly excludes GUI from MVP. ImGui integration is **future scope** — research is included here for planning, but **the MVP should focus on CLI only**. ImGui can be added in a later iteration.

> **Note**: The spec's "Excluded from MVP" section lists "Graphical user interface". The plan should integrate ImGui only when the project moves beyond MVP. This research ensures the architecture accommodates future GUI integration.

---

## Topic 4: CLI11 Integration

### Decision

**CLI11 via FetchContent**, header-only mode. Structure CLI with subcommands matching primary user workflows.

### Integration Method

CLI11 is a **header-only** library with first-class CMake support:

```cmake
include(FetchContent)
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(CLI11)

target_link_libraries(canmatik PRIVATE CLI11::CLI11)
```

Alternative: download `CLI11.hpp` single-header release and drop into `third_party/CLI11/`. This avoids any network dependency at configure time.

### MinGW Compatibility

**No known issues.** CLI11 is pure C++11/14/17 with no platform-specific code. It compiles cleanly with GCC (MinGW), Clang, and MSVC. The CI matrix for CLI11 includes GCC on multiple platforms.

### Subcommand Structure — Best Practices

Based on the spec's user stories, the recommended CLI structure for CANmatik:

```
canmatik scan          # Discover J2534 providers (User Story 1)
canmatik monitor       # Start CAN monitoring session (User Story 2)
canmatik record        # Start recording (User Story 4)
canmatik replay        # Open and inspect a saved log (User Story 5)
canmatik status        # Show session info (User Story 6)
canmatik demo          # Run with mock backend (User Story 7)
```

CLI11 subcommand pattern:

```cpp
CLI::App app{"CANmatik — J2534 CAN Bus Scanner & Logger"};
app.require_subcommand(1);

auto* scan = app.add_subcommand("scan", "Discover J2534 providers");

auto* monitor = app.add_subcommand("monitor", "Start CAN monitoring");
std::string provider;
uint32_t bitrate = 500000;
monitor->add_option("-p,--provider", provider, "J2534 provider name")->required();
monitor->add_option("-b,--bitrate", bitrate, "CAN bitrate (bps)")
       ->default_val(500000)
       ->check(CLI::IsMember({250000, 500000, 1000000}));

auto* record = app.add_subcommand("record", "Record CAN traffic to file");
std::string output_file;
std::string format = "asc";
record->add_option("-o,--output", output_file, "Output file path")->required();
record->add_option("-f,--format", format, "Output format")
      ->default_val("asc")
      ->check(CLI::IsMember({"asc", "jsonl"}));

// Parse
CLI11_PARSE(app, argc, argv);
```

**Best practices**:
- Use `->required()` for mandatory options and `->default_val()` for sensible defaults (per spec: "Minimal configuration: sensible defaults").
- Use `CLI::IsMember` validators for enumerated choices (bitrates, formats).
- Use `->group("Connection")` to organize `--help` output into logical sections.
- Share common options (e.g., `--provider`, `--bitrate`) between subcommands using CLI11's option groups or parent app options.
- Use callbacks (`->callback(...)`) or check `app.got_subcommand(...)` to dispatch to handler functions.

### Alternatives

- **Boost.Program_options**: Heavier dependency, not header-only, more verbose API. No advantage over CLI11.
- **cxxopts**: Header-only, simpler, but lacks built-in subcommand support.
- **argparse (C++)**: Newer, header-only, but less mature than CLI11 and smaller community.
- **Manual `getopt`**: Too low-level for a multi-subcommand tool.

---

## Topic 5: JSON Library for C++

### Decision

**nlohmann/json** — header-only, CMake-native, trivial API, proven MinGW compatibility.

### Comparison

| Criterion | nlohmann/json | RapidJSON | simdjson |
|---|---|---|---|
| **Ease of use** | Excellent — intuitive C++ syntax, `json j; j["key"] = value;`, automatic type conversion, range-based for loops | Good — DOM and SAX API, more verbose, manual memory management with allocator | Read-only (parsing only), must convert to another structure for modification |
| **CMake integration** | `FetchContent` or `find_package`; `nlohmann_json::nlohmann_json` target | `FetchContent` or `find_package`; works but less standardized CMake config | `FetchContent`; requires specific CPU instruction support |
| **MinGW compatibility** | No issues — pure C++11, no platform dependencies | No issues — pure C++, but some older versions had MSVC-specific workarounds | Requires SSE4.2 or AVX2 at minimum; works on MinGW but adds ISA requirement |
| **Header-only** | Yes (single header `json.hpp` ~800KB, or split headers) | Yes (header-only by default) | No — compiled library with SIMD kernels |
| **Performance** | Moderate — fine for structured log output; not designed for parsing gigabytes/sec | Fast — 2–10× faster than nlohmann for large documents | Fastest — GB/s parsing; overkill for log output |
| **Output (serialization)** | Excellent — `dump()` with indent control, ordered output option | Good — `Writer` / `PrettyWriter` API | No serialization — parse only |
| **Community** | Massive adoption (~43k GitHub stars), de facto standard for C++ JSON | Large adoption (~14k stars), popular in game engines and performance-critical code | Growing (~19k stars), focused on parsing-heavy workloads |

**Why nlohmann/json**: The primary JSON use case is **structured log output** (serialization of CAN frame data, session metadata). nlohmann/json's API is the most expressive for constructing and writing JSON. Performance is irrelevant — even a sustained 8000 frames/sec CAN bus produces only ~8000 small JSON objects/sec, well within nlohmann/json's throughput. The library is a single header drop-in with zero configuration.

For reading back JSON log files (User Story 5: offline inspection), nlohmann/json's streaming parser (`json::parse(stream)`) handles million-frame files efficiently when combined with SAX or streaming mode (`json::sax_parse`).

### Integration

```cmake
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

target_link_libraries(canmatik PRIVATE nlohmann_json::nlohmann_json)
```

### Alternatives

- **RapidJSON**: Choose if profiling reveals nlohmann/json is a bottleneck for large log file parsing (unlikely).
- **simdjson**: Choose if the project later needs to parse external multi-GB JSON datasets. Not suitable for serialization.
- **{fmt} + manual**: For trivially simple JSON output, `fmt::format` can produce JSON strings directly. But this doesn't scale and loses type safety.

---

## Topic 6: CAN Frame Timestamp Strategy

### Decision

**Dual-timestamp approach**: Use the J2534 adapter's hardware timestamp as the primary source; supplement with `QueryPerformanceCounter` (via `std::chrono::steady_clock`) as the host-side monotonic reference. Log both when available.

### Windows Monotonic Clock Sources

| Source | Resolution | Monotonic? | Cost | Notes |
|---|---|---|---|---|
| `QueryPerformanceCounter` (QPC) | Sub-microsecond (typically 100 ns or 10 MHz) | Yes — guaranteed monotonic since Windows Vista | ~20–40 ns per call | The standard choice for high-resolution timing on Windows. Backed by TSC on modern hardware. |
| `std::chrono::steady_clock` | Implementation-defined; on MinGW-w64/GCC it typically wraps QPC | Yes (by specification) | Same as QPC (since it wraps QPC) | Portable C++ API. On MinGW-w64, `steady_clock` uses QPC internally. Preferred for portability. |
| `GetTickCount64` | 15.6 ms (system timer resolution) | Yes | Cheap | Far too coarse for CAN frame timing |
| `timeGetTime` | 1 ms (with `timeBeginPeriod`) | Not guaranteed monotonic | Cheap | Legacy; not recommended |

**Recommendation**: Use `std::chrono::steady_clock` for host timestamps. It wraps QPC on MinGW-w64, provides sub-microsecond resolution, and is portable C++20. No need to call QPC directly.

```cpp
auto host_ts = std::chrono::steady_clock::now();
auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
    host_ts.time_since_epoch()
).count();
```

### J2534 Adapter Timestamp

The `PASSTHRU_MSG` struct contains a `Timestamp` field:

```c
typedef struct {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;    // microseconds, adapter clock
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[4128];
} PASSTHRU_MSG;
```

| Aspect | Detail |
|---|---|
| **Unit** | Microseconds (per J2534 spec) |
| **Resolution** | Adapter-dependent. Typical: 1 µs (hardware timer). Some cheap adapters: 1 ms. |
| **Epoch** | Adapter-dependent. Usually time since adapter power-on or driver load. **Not** wall-clock time. |
| **Rollover** | `unsigned long` = 32 bits → rolls over every ~4295 seconds (~71.6 minutes). Application must handle rollover for long sessions. |
| **Reliability** | Varies significantly across adapters. Well-known adapters (Tactrix, Drew Technologies, Bosch) provide accurate hardware timestamps. Cheap clones may have jitter or fixed-rate timestamps. |
| **Relative accuracy** | Good for inter-frame timing (delta between consecutive frames) even on mediocre adapters. |

### Best Practice — Dual Timestamp

```
Frame {
    adapter_timestamp_us: uint64_t   // From PASSTHRU_MSG.Timestamp (extended to 64-bit to handle rollover)
    host_timestamp_us:    uint64_t   // From steady_clock at moment PassThruReadMsgs returns
    ...
}
```

**Why both**:
- **Adapter timestamp**: Best source for inter-frame timing and precise bus-level event ordering. Hardware-timestamped at the adapter's CAN controller, not subject to OS scheduling jitter.
- **Host timestamp**: Provides wall-clock correlation (session start time + offset), detects adapter timestamp anomalies, and serves as fallback if adapter timestamps are unreliable or zero.

**Rollover handling**: Track the previous adapter timestamp. If the new value is less than the previous, increment a rollover counter. Compute `extended_ts = rollover_count * 0x100000000ULL + raw_ts`.

**Session-relative display**: Display timestamps as offset from session start (e.g., `+0.000000`, `+0.001234`) rather than raw epoch values. This matches existing CAN tool conventions (Vector CANalyzer, PCAN-View).

### Alternatives

- **Adapter timestamp only**: Simpler, but loses wall-clock reference and is vulnerable to adapter bugs.
- **Host timestamp only**: Simpler, but adds 0.5–5 ms of jitter from OS scheduling and USB latency. Unacceptable for precise inter-frame timing analysis.
- **NTP/wall-clock**: Not monotonic; subject to adjustments. Never use for inter-frame timing.

---

## Topic 7: Log Format Design for CAN Captures

### Decision

**Primary format: Vector ASC** (human-readable text). **Secondary format: JSONL** (machine-readable structured). Both are produced by the recorder; the user selects via `--format` flag.

### Existing CAN Log Formats

| Format | Origin | Structure | Ecosystem Support | Suitability |
|---|---|---|---|---|
| **Vector ASC** | Vector Informatik (CANalyzer/CANoe) | Plain text, one frame per line, timestamped | Universal — supported by almost every CAN analysis tool (SavvyCAN, BusMaster, python-can, etc.) | Excellent — de facto standard for interchange |
| **PEAK TRC** | PEAK-System (PCAN) | Semi-structured text, header + frame lines | PCAN tools, python-can, SavvyCAN | Good but PEAK-specific |
| **BLF (Binary Logging Format)** | Vector Informatik | Binary, compressed | CANalyzer/CANoe native; python-can can read | Not human-readable; complex to implement |
| **candump format** | SocketCAN (Linux) | Minimal text: `(timestamp) interface canid#data` | Linux CAN tooling, python-can | Very simple; lacks metadata |
| **SavvyCAN CSV** | SavvyCAN | CSV with headers | SavvyCAN, spreadsheets | Simple but no standard header metadata |
| **MDF4** | ASAM | Binary, structured, multi-signal | Professional tools | Extremely complex; overkill for MVP |

### Format Decision Rationale

**Vector ASC as primary text format**:
- Universally recognized by CAN analysis tools: load directly into SavvyCAN, BusMaster, python-can, MATLAB Vehicle Network Toolbox.
- Human-readable in any text editor.
- Simple to implement (line-oriented format, no binary encoding).
- Well-documented format (many open-source parsers serve as reference).

ASC format example:
```
date Sun Mar  8 14:30:00.000 2026
base hex  timestamps absolute
internal events logged
Begin Triggerblock Sun Mar  8 14:30:00.000 2026
   0.000000 1  7E8             Rx   d 8 02 41 0C 1A F8 00 00 00
   0.001234 1  7E0             Rx   d 8 02 01 0C 00 00 00 00 00
   0.002500 1  3B0             Rx   d 4 A1 B2 C3 D4
End TriggerBlock
```

**JSONL (JSON Lines) as structured format**:
- One JSON object per line — trivial to parse with any language, streamable, grepable.
- First line is a metadata header object.
- Supports all required fields per FR-011.
- Easier to process programmatically than ASC for custom analysis pipelines.
- JSONL (newline-delimited JSON) over monolithic JSON array: allows streaming writes with no closing bracket needed, and crash-safe (partial file is still valid up to last complete line).

JSONL format example:
```json
{"_type":"header","tool":"canmatik","version":"0.1.0","adapter":"Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM","bitrate":500000,"start_time":"2026-03-08T14:30:00.000Z","start_time_unix_us":1772925000000000}
{"ts":0.000000,"ats":123456,"id":"7E8","ext":false,"type":"rx","dlc":8,"data":"0241 0C1A F800 0000"}
{"ts":0.001234,"ats":124690,"id":"7E0","ext":false,"type":"rx","dlc":8,"data":"0201 0C00 0000 0000"}
```

### Log File Header Metadata

The header (first lines of ASC comment block, or first JSON object in JSONL) should contain:

| Field | Description | Example |
|---|---|---|
| `tool` | Application name and version | `canmatik 0.1.0` |
| `format_version` | Log format version (for future compatibility) | `1.0` |
| `adapter_name` | J2534 provider/adapter name | `Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM` |
| `adapter_vendor` | Vendor name | `Tactrix` |
| `protocol` | Protocol identifier | `CAN` |
| `bitrate` | Configured bitrate | `500000` |
| `start_time_utc` | Session start in ISO 8601 UTC | `2026-03-08T14:30:00.000Z` |
| `start_time_unix_us` | Session start as Unix microseconds | `1772925000000000` |
| `host_os` | Operating system | `Windows 10 22H2` |
| `filters` | Active filters at recording start | `[{"type":"pass","id":"0x7E8"}]` |
| `comment` | User-supplied comment (optional) | `Ducati Panigale V4 idle capture` |

### Should the Project Define Its Own Format?

**No — adopt existing formats.** The spec emphasizes interoperability ("structured export for external tooling") and the user personas explicitly need to share captures with the community and feed data into analysis pipelines. Inventing a proprietary format creates friction. ASC + JSONL covers both human and machine consumers.

A **future** format could be added (e.g., CSV for spreadsheet users, or BLF for Vector tool users) without changing the core architecture — the recorder produces frames from a queue, and the formatter is a pluggable strategy.

### Alternatives

- **CSV**: Simpler than JSONL but loses nested metadata and requires header-row conventions. Consider as a third format option.
- **PEAK TRC**: Viable alternative to ASC but less universally supported.
- **Custom binary format**: Only if performance profiling shows text I/O is a bottleneck for sustained high-rate logging (unlikely — modern SSDs handle 100+ MB/s easily).

---

## Topic 8: Thread Model for Real-Time CAN Capture in C++

### Decision

**Dedicated reader thread** polling `PassThruReadMsgs` in a tight loop, pushing frames into a **lock-free SPSC (single-producer, single-consumer) ring buffer**. Consumer thread (CLI display / file writer) drains the buffer. Signaling via `std::condition_variable` for non-latency-critical consumers, or busy-poll for the recorder.

### Architecture

```
┌──────────────┐     lock-free      ┌──────────────────┐
│ Reader Thread │───── SPSC ────────▶│ Consumer Thread   │
│               │     ring buf      │ (CLI + Recorder)  │
│ PassThruRead  │                   │                    │
│ Msgs (poll)   │                   │ Display + Write    │
└──────────────┘                    └──────────────────┘
      │                                      │
      │ shutdown                             │ config changes
      ▼                                      ▼
  std::atomic<bool>                   std::atomic<bool>
    running_                           recording_
```

### Reader Thread Design

```cpp
void reader_thread_func(J2534Device& device, SPSCQueue<CanFrame>& queue,
                        std::atomic<bool>& running) {
    PASSTHRU_MSG msgs[16];  // batch read up to 16 messages
    while (running.load(std::memory_order_relaxed)) {
        unsigned long count = 16;
        long ret = device.PassThruReadMsgs(channelId, msgs, &count, 100 /*ms timeout*/);

        if (ret == STATUS_NOERROR || ret == ERR_BUFFER_EMPTY || ret == ERR_TIMEOUT) {
            for (unsigned long i = 0; i < count; ++i) {
                CanFrame frame = convert(msgs[i]);
                frame.host_timestamp = steady_clock::now();
                queue.try_push(frame);  // drop on overflow (log warning)
            }
        } else {
            // Handle error (disconnect, device error)
            handle_error(ret);
        }
    }
}
```

**Key points**:
- `PassThruReadMsgs` with a timeout of 50–100 ms ensures the thread doesn't spin burn CPU on a quiet bus, but wakes promptly when frames arrive.
- Batch reading (up to 16 messages per call) reduces syscall overhead under high load.
- The 100 ms timeout provides a natural check interval for the `running` flag.

### Thread-Safe Queue Options

| Option | Type | Pros | Cons |
|---|---|---|---|
| **Lock-free SPSC ring buffer** | Single-producer single-consumer | Zero contention; ~10 ns per operation; no mutex; no priority inversion | Exactly 1 producer + 1 consumer; fixed capacity; must handle full-buffer policy |
| **`std::mutex` + `std::deque`** | Multi-producer multi-consumer | Simple; flexible; unbounded | Mutex contention under high load; ~50–100 ns per operation; potential priority inversion |
| **`moodycamel::ConcurrentQueue`** | MPMC lock-free | Battle-tested; header-only; MPMC capable | More complex; MPMC is overkill for this architecture |
| **`boost::lockfree::spsc_queue`** | SPSC lock-free | Well-tested; standard API | Boost dependency |

**Recommendation**: **Custom or vendored SPSC ring buffer**. The architecture is naturally SPSC (one reader thread produces, one consumer thread consumes). A simple implementation is ~50 lines of C++ using `std::atomic` with `memory_order_acquire` / `memory_order_release`. Alternatively, use `moodycamel::ReaderWriterQueue` (single-header, MIT license, optimized SPSC).

For MVP simplicity, **`std::mutex` + `std::queue`** is perfectly adequate. At 8000 frames/sec (100% CAN bus at 500 kbps), mutex overhead is negligible. Upgrade to lock-free only if profiling shows contention.

### Signaling Between Threads

| Mechanism | Use Case |
|---|---|
| **`std::condition_variable`** | Consumer sleeps when queue is empty; reader notifies after pushing. Good for CLI display that doesn't need sub-ms latency. |
| **Busy-poll with `std::this_thread::yield()`** | File recorder that needs lowest latency. Drains queue in a tight loop. |
| **`std::atomic<bool>` flags** | Shutdown signal (`running_`), recording toggle (`recording_`), filter update flag. |

**Pattern for CLI consumer**:

```cpp
void consumer_thread_func(SPSCQueue<CanFrame>& queue,
                          std::condition_variable& cv,
                          std::mutex& cv_mutex,
                          std::atomic<bool>& running) {
    while (running.load()) {
        CanFrame frame;
        if (queue.try_pop(frame)) {
            display(frame);
            if (recording_.load()) {
                write_to_log(frame);
            }
        } else {
            std::unique_lock<std::mutex> lock(cv_mutex);
            cv.wait_for(lock, std::chrono::milliseconds(100));
        }
    }
}
```

### MinGW Threading Support

| Option | Description | Status |
|---|---|---|
| **winpthreads** (POSIX threads) | MinGW-w64 provides a POSIX threading layer (`libwinpthread-1.dll`). Enables `std::thread`, `std::mutex`, `std::condition_variable`. | Default in most MinGW-w64 distributions (MSYS2, w64devkit). Mature and stable. |
| **MCF thread model** | Newer threading model in MinGW-w64 using native Windows primitives. Available in MSYS2 since ~2024. | Not yet universal; may not be available in all MinGW distributions. |
| **Native Win32 threads** | `CreateThread`, `CRITICAL_SECTION`, `CONDITION_VARIABLE`, `InitOnceExecuteOnce`. | Always available; no DLL dependency; but loses `std::thread` / `std::mutex` portability. |

**Recommendation**: Use the **winpthreads** (POSIX) threading model, which is the default in modern MinGW-w64 distributions (MSYS2 `mingw-w64-x86_64-gcc`). This enables full C++20 `<thread>`, `<mutex>`, `<condition_variable>`, and `<atomic>` support with no extra configuration.

**Verify at build time**:

```cpp
#if defined(__MINGW32__) && !defined(_GLIBCXX_HAS_GTHREADS)
#error "MinGW build requires POSIX threading model (winpthreads)"
#endif
```

**Runtime dependency**: The `libwinpthread-1.dll` must ship alongside the executable (or statically link with `-static`). Using `-static` is recommended for distribution to avoid DLL hell.

### Concurrency Primitives Summary

| Primitive | Use |
|---|---|
| `std::thread` | Reader thread, consumer thread |
| `std::mutex` + `std::lock_guard` | Protecting shared configuration (filter state, recording path) |
| `std::condition_variable` | Waking consumer when frames available |
| `std::atomic<bool>` | Shutdown flag, recording toggle |
| `std::atomic<uint64_t>` | Frame counters, error counters (lock-free stats) |

### Alternatives

- **Single-threaded with non-blocking poll**: Call `PassThruReadMsgs` with `timeout=0` in the main loop. Simpler but couples GUI/CLI refresh rate to poll rate. Acceptable for a CLI MVP.
- **Thread pool**: Overkill for 2 threads. No benefit.
- **Async I/O (IOCP, overlapped)**: J2534 API doesn't support async I/O; would need to wrap in a thread anyway.
- **Coroutines (C++20)**: MinGW-w64 GCC coroutine support is still maturing. Avoid for MVP.

---

## Topic 9: TinyLog Logging Library

### Decision

**[TinyLog](https://github.com/Pugnator/TinyLog)** — a singleton C++ diagnostic logger integrated as a git submodule. Handles all application-level diagnostic logging (not CAN frame capture logs).

### Rationale

| Criterion | TinyLog | spdlog | Custom |
|---|---|---|---|
| **Singleton pattern** | Built-in (`Log::get()`) | Manual setup | Manual |
| **C++20 `std::format`** | Yes — uses `std::format` internally | Uses `{fmt}` library (separate dep) | N/A |
| **Log rotation** | Built-in with configurable `RotationConfig` (max_file_size, max_backup_count, compress) | Built-in via rotating file sink | Manual |
| **Compression** | zstd compression of rotated files (vendored in repo) | No built-in compression | Manual |
| **Windows console coloring** | Yes — `WriteConsoleA` + `SetConsoleTextAttribute` | Yes | Manual |
| **Thread safety** | Mutex-protected writes, `shared_mutex` for configure/log | Thread-safe | Manual |
| **Build complexity** | Moderate — see below | Low (header-only option) | None |
| **Dependencies** | zstd (vendored within TinyLog repo) | `{fmt}` (bundled or external) | None |

**Why TinyLog over spdlog**: TinyLog's built-in zstd rotation and singleton pattern match the project's needs without adding another dependency. The project already uses C++20 `std::format`, so TinyLog's use of it is natural. The compact API (`LOG_INFO(...)`, `LOG_DEBUG(...)`, `LOG_CALL(...)`, `LOG_EXCEPTION(...)`) is sufficient for diagnostic output.

### API Surface

**Severity bitmask** (values are powers of 2, combinable via bitwise OR):

| Level | Value | Macro | Use |
|---|:---:|---|---|
| `info` | 1 | `LOG_INFO(...)` | Normal operational messages |
| `warning` | 2 | — (use `Log::get()->log(TraceSeverity::warning, ...)`) | Non-fatal issues |
| `error` | 4 | — | Recoverable errors |
| `debug` | 8 | `LOG_DEBUG(...)` | Detailed diagnostic output |
| `verbose` | 16 | `LOG_CALL(...)` | Function entry/exit tracing |
| `critical` | 32 | `LOG_EXCEPTION(...)` | Fatal/unrecoverable errors |

**Trace types**: `TraceType::devnull` (discard), `TraceType::console` (colored stdout), `TraceType::file` (file with optional rotation).

**Initialization pattern**:

```cpp
auto& logger = Log::get();
logger->addTracer(TraceType::console);  // Always: colored console output
if (config.debug) {
    logger->addTracer(TraceType::file);
    logger->setLogFile("canmatik.log");
    RotationConfig rot;
    rot.max_file_size = config.log_max_file_size;
    rot.max_backup_count = config.log_max_backups;
    rot.compress = config.log_compress;
    logger->setRotationConfig(rot);
}
```

### Build Integration — Critical Workaround

**Problem**: TinyLog's own `CMakeLists.txt` hardcodes `add_library(tinyLog SHARED ...)`. There is no CMake option or `BUILD_SHARED_LIBS` check to override this. Since the project requires static linking (`-static`) for portable distribution, using TinyLog's CMake directly is not viable.

**Solution**: Bypass TinyLog's CMakeLists.txt entirely. In `third_party/CMakeLists.txt`, compile `tinylog/log.cc` directly as a STATIC library and link zstd statically:

```cmake
# Build zstd as static library
add_library(libzstd_static STATIC
    tinylog/vendor/zstd/lib/common/*.c
    tinylog/vendor/zstd/lib/compress/*.c
    tinylog/vendor/zstd/lib/decompress/*.c
)

# Compile TinyLog as STATIC (bypasses hardcoded SHARED)
add_library(tinylog STATIC tinylog/log.cc)
target_link_libraries(tinylog PUBLIC libzstd_static)
target_include_directories(tinylog PUBLIC tinylog/)
```

**Note**: Only `log.cc` is needed. `dllmain.cc` is DLL-specific entry point code and must be excluded.

### Submodule Setup

```bash
git submodule add https://github.com/Pugnator/TinyLog.git third_party/tinylog
git submodule update --init --recursive  # Fetches vendored zstd
```

### Alternatives

- **spdlog**: More popular, but adds `{fmt}` dependency and lacks built-in zstd rotation. A valid alternative if TinyLog's build workaround proves too fragile.
- **Boost.Log**: Heavy dependency, overkill for diagnostic logging.
- **Custom logger**: Would require reimplementing rotation, coloring, and thread safety. Not justified when TinyLog covers the need.

---

## Summary of Decisions

| # | Topic | Decision | Key Rationale |
|---|---|---|---|
| 1 | Testing Framework | **Catch2 v3** | Clean MinGW builds, BDD macros match spec scenarios, excellent CMake integration |
| 2 | J2534 DLL Loading | **LoadLibrary + GetProcAddress**, 32-bit build, custom header | Runtime binding is the only option; 32-bit ensures universal adapter compatibility |
| 3 | ImGui Backend | **Win32 + OpenGL3** (future scope, not MVP) | Zero external deps with MinGW; DirectX headers incomplete on MinGW |
| 4 | CLI Framework | **CLI11** via FetchContent | Header-only, subcommand support, zero MinGW issues |
| 5 | JSON Library | **nlohmann/json** | Best DX for serialization, header-only, massive community, proven MinGW compat |
| 6 | Timestamp Strategy | **Dual: adapter µs + host steady_clock** | Adapter for inter-frame precision, host for wall-clock correlation and fallback |
| 7 | Log Format | **Vector ASC (text) + JSONL (structured)** | ASC is universally supported by CAN tools; JSONL is streamable and crash-safe |
| 8 | Threading Model | **Reader thread + SPSC queue + condition_variable** | Natural producer-consumer; MinGW winpthreads enables full C++20 threading |
| 9 | Logging Library | **TinyLog** (git submodule, compiled as STATIC) | Singleton logger with zstd rotation; C++20 `std::format`; bypasses hardcoded SHARED via direct `log.cc` compilation |
