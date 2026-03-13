/// @file main.cpp
/// Unified CANmatik entry point.
/// - No arguments  -> launch GUI
/// - Arguments     -> run CLI (scan, monitor, record, replay, etc.)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <fcntl.h>

// CLI entry point (src/cli/main.cpp logic, extracted)
int cli_main(int argc, char** argv);

// GUI entry point (src/gui/gui_main.cpp logic, extracted)
int gui_main(HINSTANCE hInstance, int nCmdShow);

// ---------------------------------------------------------------------------

/// Attach to the parent's console so that CLI output is visible when
/// launched from cmd.exe / PowerShell.  When stdout is already a pipe
/// (e.g. spawned by _popen / CreateProcess with redirected handles),
/// we leave the CRT handles alone so pipe I/O works normally.
static void attach_console() {
    // If stdout is already a valid handle (pipe, file, etc.), skip console
    // attachment — the parent redirected our output.
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != nullptr) {
        DWORD fileType = GetFileType(hStdOut);
        if (fileType == FILE_TYPE_PIPE || fileType == FILE_TYPE_DISK) {
            // Output already redirected — nothing to do.
            return;
        }
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
    } else {
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow) {
    // __argc / __argv are provided by the MSVC/MinGW CRT even for WinMain
    if (__argc > 1) {
        attach_console();
        return cli_main(__argc, __argv);
    }

    return gui_main(hInstance, nCmdShow);
}
