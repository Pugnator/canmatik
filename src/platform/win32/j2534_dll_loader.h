#pragma once

/// @file j2534_dll_loader.h
/// RAII DLL loader for J2534 — LoadLibrary/GetProcAddress/FreeLibrary (T026 — US1).

#include "platform/win32/j2534_defs.h"
#include <string>

#ifndef _WIN32
#error "j2534_dll_loader.h is a Windows-only header"
#endif

#include <windows.h>

namespace canmatik {

/// RAII wrapper that loads a J2534 DLL and resolves all standard entry points.
/// On destruction the DLL is freed automatically.
class J2534DllLoader {
public:
    J2534DllLoader() = default;
    ~J2534DllLoader();

    // Non-copyable, movable
    J2534DllLoader(const J2534DllLoader&) = delete;
    J2534DllLoader& operator=(const J2534DllLoader&) = delete;
    J2534DllLoader(J2534DllLoader&& other) noexcept;
    J2534DllLoader& operator=(J2534DllLoader&& other) noexcept;

    /// Load the DLL at the given path and resolve all J2534 function pointers.
    /// @throws TransportError on LoadLibrary failure or missing mandatory exports.
    void load(const std::string& dll_path);

    /// Unload the DLL and clear all function pointers.
    void unload();

    /// Check whether a DLL is currently loaded.
    [[nodiscard]] bool is_loaded() const { return module_ != nullptr; }

    /// The path of the currently loaded DLL.
    [[nodiscard]] const std::string& path() const { return path_; }

    // -----------------------------------------------------------------------
    // Resolved J2534 function pointers (nullptr if not loaded)
    // -----------------------------------------------------------------------
    j2534::PassThruOpen_t           PassThruOpen           = nullptr;
    j2534::PassThruClose_t          PassThruClose          = nullptr;
    j2534::PassThruConnect_t        PassThruConnect        = nullptr;
    j2534::PassThruDisconnect_t     PassThruDisconnect     = nullptr;
    j2534::PassThruReadMsgs_t       PassThruReadMsgs       = nullptr;
    j2534::PassThruWriteMsgs_t      PassThruWriteMsgs      = nullptr;
    j2534::PassThruStartMsgFilter_t PassThruStartMsgFilter = nullptr;
    j2534::PassThruStopMsgFilter_t  PassThruStopMsgFilter  = nullptr;
    j2534::PassThruStartPeriodicMsg_t PassThruStartPeriodicMsg = nullptr;
    j2534::PassThruStopPeriodicMsg_t  PassThruStopPeriodicMsg  = nullptr;
    j2534::PassThruIoctl_t          PassThruIoctl          = nullptr;
    j2534::PassThruReadVersion_t    PassThruReadVersion    = nullptr;
    j2534::PassThruGetLastError_t   PassThruGetLastError   = nullptr;

private:
    HMODULE module_ = nullptr;
    std::string path_;

    /// Resolve a single export by name.
    /// @return The function address, or nullptr if not found.
    [[nodiscard]] FARPROC resolve(const char* name) const;
};

} // namespace canmatik
