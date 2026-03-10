/// @file j2534_dll_loader.cpp
/// RAII DLL loader for J2534 (T026 — US1).

#include "platform/win32/j2534_dll_loader.h"
#include "transport/transport_error.h"

#include "core/log_macros.h"

namespace canmatik {

J2534DllLoader::~J2534DllLoader() {
    unload();
}

J2534DllLoader::J2534DllLoader(J2534DllLoader&& other) noexcept
    : module_(other.module_), path_(std::move(other.path_)),
      PassThruOpen(other.PassThruOpen), PassThruClose(other.PassThruClose),
      PassThruConnect(other.PassThruConnect), PassThruDisconnect(other.PassThruDisconnect),
      PassThruReadMsgs(other.PassThruReadMsgs), PassThruWriteMsgs(other.PassThruWriteMsgs),
      PassThruStartMsgFilter(other.PassThruStartMsgFilter),
      PassThruStopMsgFilter(other.PassThruStopMsgFilter),
      PassThruStartPeriodicMsg(other.PassThruStartPeriodicMsg),
      PassThruStopPeriodicMsg(other.PassThruStopPeriodicMsg),
      PassThruIoctl(other.PassThruIoctl),
      PassThruReadVersion(other.PassThruReadVersion),
      PassThruGetLastError(other.PassThruGetLastError)
{
    other.module_ = nullptr;
    other.PassThruOpen = nullptr;
    other.PassThruClose = nullptr;
    other.PassThruConnect = nullptr;
    other.PassThruDisconnect = nullptr;
    other.PassThruReadMsgs = nullptr;
    other.PassThruWriteMsgs = nullptr;
    other.PassThruStartMsgFilter = nullptr;
    other.PassThruStopMsgFilter = nullptr;
    other.PassThruStartPeriodicMsg = nullptr;
    other.PassThruStopPeriodicMsg = nullptr;
    other.PassThruIoctl = nullptr;
    other.PassThruReadVersion = nullptr;
    other.PassThruGetLastError = nullptr;
}

J2534DllLoader& J2534DllLoader::operator=(J2534DllLoader&& other) noexcept {
    if (this != &other) {
        unload();
        module_ = other.module_;
        path_ = std::move(other.path_);
        PassThruOpen = other.PassThruOpen;
        PassThruClose = other.PassThruClose;
        PassThruConnect = other.PassThruConnect;
        PassThruDisconnect = other.PassThruDisconnect;
        PassThruReadMsgs = other.PassThruReadMsgs;
        PassThruWriteMsgs = other.PassThruWriteMsgs;
        PassThruStartMsgFilter = other.PassThruStartMsgFilter;
        PassThruStopMsgFilter = other.PassThruStopMsgFilter;
        PassThruStartPeriodicMsg = other.PassThruStartPeriodicMsg;
        PassThruStopPeriodicMsg = other.PassThruStopPeriodicMsg;
        PassThruIoctl = other.PassThruIoctl;
        PassThruReadVersion = other.PassThruReadVersion;
        PassThruGetLastError = other.PassThruGetLastError;
        other.module_ = nullptr;
        other.PassThruOpen = nullptr;
        other.PassThruClose = nullptr;
        other.PassThruConnect = nullptr;
        other.PassThruDisconnect = nullptr;
        other.PassThruReadMsgs = nullptr;
        other.PassThruWriteMsgs = nullptr;
        other.PassThruStartMsgFilter = nullptr;
        other.PassThruStopMsgFilter = nullptr;
        other.PassThruStartPeriodicMsg = nullptr;
        other.PassThruStopPeriodicMsg = nullptr;
        other.PassThruIoctl = nullptr;
        other.PassThruReadVersion = nullptr;
        other.PassThruGetLastError = nullptr;
    }
    return *this;
}

void J2534DllLoader::load(const std::string& dll_path) {
    unload();

    LOG_DEBUG("Loading J2534 DLL: {}", dll_path);
    module_ = ::LoadLibraryA(dll_path.c_str());
    if (!module_) {
        DWORD err = ::GetLastError();
        std::string msg = "Failed to load J2534 DLL '" + dll_path +
                          "' (Windows error " + std::to_string(err) + ")";
        LOG_ERROR("{}", msg);
        throw TransportError(static_cast<int32_t>(err), msg, "LoadLibrary");
    }
    path_ = dll_path;
    LOG_DEBUG("J2534 DLL loaded successfully: {}", dll_path);

    // Resolve mandatory exports
    PassThruOpen           = reinterpret_cast<j2534::PassThruOpen_t>(resolve("PassThruOpen"));
    PassThruClose          = reinterpret_cast<j2534::PassThruClose_t>(resolve("PassThruClose"));
    PassThruConnect        = reinterpret_cast<j2534::PassThruConnect_t>(resolve("PassThruConnect"));
    PassThruDisconnect     = reinterpret_cast<j2534::PassThruDisconnect_t>(resolve("PassThruDisconnect"));
    PassThruReadMsgs       = reinterpret_cast<j2534::PassThruReadMsgs_t>(resolve("PassThruReadMsgs"));
    PassThruWriteMsgs      = reinterpret_cast<j2534::PassThruWriteMsgs_t>(resolve("PassThruWriteMsgs"));
    PassThruStartMsgFilter = reinterpret_cast<j2534::PassThruStartMsgFilter_t>(resolve("PassThruStartMsgFilter"));
    PassThruStopMsgFilter  = reinterpret_cast<j2534::PassThruStopMsgFilter_t>(resolve("PassThruStopMsgFilter"));
    PassThruIoctl          = reinterpret_cast<j2534::PassThruIoctl_t>(resolve("PassThruIoctl"));
    PassThruGetLastError   = reinterpret_cast<j2534::PassThruGetLastError_t>(resolve("PassThruGetLastError"));

    // Optional but expected exports
    PassThruReadVersion      = reinterpret_cast<j2534::PassThruReadVersion_t>(resolve("PassThruReadVersion"));
    PassThruStartPeriodicMsg = reinterpret_cast<j2534::PassThruStartPeriodicMsg_t>(resolve("PassThruStartPeriodicMsg"));
    PassThruStopPeriodicMsg  = reinterpret_cast<j2534::PassThruStopPeriodicMsg_t>(resolve("PassThruStopPeriodicMsg"));

    // Validate mandatory exports
    if (!PassThruOpen || !PassThruClose || !PassThruConnect || !PassThruDisconnect ||
        !PassThruReadMsgs || !PassThruWriteMsgs || !PassThruStartMsgFilter ||
        !PassThruStopMsgFilter || !PassThruIoctl || !PassThruGetLastError) {
        std::string msg = "J2534 DLL '" + dll_path + "' is missing mandatory exports";
        LOG_ERROR("{}", msg);
        unload();
        throw TransportError(0, msg, "GetProcAddress");
    }

    LOG_DEBUG("All mandatory J2534 exports resolved for: {}", dll_path);
    if (!PassThruReadVersion) {
        LOG_WARNING("PassThruReadVersion not found in {} — version query unavailable", dll_path);
    }
}

void J2534DllLoader::unload() {
    if (module_) {
        LOG_DEBUG("Unloading J2534 DLL: {}", path_);
        ::FreeLibrary(module_);
        module_ = nullptr;
    }
    path_.clear();
    PassThruOpen = nullptr;
    PassThruClose = nullptr;
    PassThruConnect = nullptr;
    PassThruDisconnect = nullptr;
    PassThruReadMsgs = nullptr;
    PassThruWriteMsgs = nullptr;
    PassThruStartMsgFilter = nullptr;
    PassThruStopMsgFilter = nullptr;
    PassThruStartPeriodicMsg = nullptr;
    PassThruStopPeriodicMsg = nullptr;
    PassThruIoctl = nullptr;
    PassThruReadVersion = nullptr;
    PassThruGetLastError = nullptr;
}

FARPROC J2534DllLoader::resolve(const char* name) const {
    if (!module_) return nullptr;
    FARPROC addr = ::GetProcAddress(module_, name);
    if (addr) {
        LOG_DEBUG("  Resolved: {}", name);
    } else {
        LOG_DEBUG("  NOT found: {}", name);
    }
    return addr;
}

} // namespace canmatik
