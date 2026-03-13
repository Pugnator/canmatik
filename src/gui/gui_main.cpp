/// @file gui_main.cpp
/// CANmatik GUI entry point — ImGui + Win32/OpenGL3, powered by GuiApp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include "gui/gui_app.h"
#include "core/log_macros.h"

#include <string>
#include <filesystem>
#include <stdexcept>
#include <shlobj.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Minimal WGL helpers (no GLEW/GLAD needed for OpenGL 2.x / imgui_impl_opengl3)
// ---------------------------------------------------------------------------

static HGLRC g_hRC  = nullptr;
static HDC   g_hDC  = nullptr;

static bool CreateGLContext(HWND hWnd) {
    g_hDC = GetDC(hWnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    int pf = ChoosePixelFormat(g_hDC, &pfd);
    if (!pf) return false;
    if (!SetPixelFormat(g_hDC, pf, &pfd)) return false;

    g_hRC = wglCreateContext(g_hDC);
    if (!g_hRC) return false;
    wglMakeCurrent(g_hDC, g_hRC);
    return true;
}

static void CleanupGL(HWND hWnd) {
    wglMakeCurrent(nullptr, nullptr);
    if (g_hRC) { wglDeleteContext(g_hRC); g_hRC = nullptr; }
    if (g_hDC) { ReleaseDC(hWnd, g_hDC); g_hDC = nullptr; }
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// Writable app-data directory: %LOCALAPPDATA%\CANmatik
// Avoids UAC errors when installed under Program Files.
// ---------------------------------------------------------------------------
static std::filesystem::path get_appdata_dir() {
    char buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) {
        auto dir = std::filesystem::path(buf) / "CANmatik";
        std::filesystem::create_directories(dir);
        return dir;
    }
    // Fallback: %TEMP%
    if (const char* tmp = std::getenv("TEMP"))
        return std::filesystem::path(tmp);
    return std::filesystem::path(".");
}

// ---------------------------------------------------------------------------
// gui_main — called from the unified entry point
// ---------------------------------------------------------------------------
int gui_main(HINSTANCE hInstance, int nCmdShow) {
  try {
    // Writable directory for logs and settings
    auto app_dir = get_appdata_dir();

    // Initialize TinyLog: console + file output
    {
        auto& logger = Log::get();
        logger.reset_levels();
        logger.set_level(TraceSeverity::info)
              .set_level(TraceSeverity::warning)
              .set_level(TraceSeverity::error)
              .set_level(TraceSeverity::critical)
              .set_level(TraceSeverity::debug)
              .set_level(TraceSeverity::verbose);
        logger.configure(TraceType::console);
        RotationConfig rotation;
        rotation.max_file_size    = 10485760;
        rotation.max_backup_count = 5;
        rotation.compress         = false;
        auto log_path = (app_dir / "canmatik_gui.log").string();
        logger.configure(TraceType::file, log_path, rotation);
        LOG_INFO("CANmatik GUI starting");
        LOG_INFO("App data dir: %s", app_dir.string().c_str());
    }

    // Settings file in writable directory
    std::string settings_path = (app_dir / "canmatik_gui.json").string();

    // GuiApp manages all state
    canmatik::GuiApp app;
    app.init(settings_path);

    // Register window class
    const wchar_t CLASS_NAME[] = L"CANmatikGUI";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, L"IDI_ICON1");
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    // Create window
    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"CANmatik GUI",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        app.window_width(), app.window_height(),
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 1;

    // OpenGL context
    if (!CreateGLContext(hWnd)) {
        MessageBoxW(hWnd, L"Failed to create OpenGL context.\n\n"
                    L"Make sure your graphics driver supports OpenGL 2.0+.\n"
                    L"Update your GPU driver and try again.",
                    L"CANmatik — OpenGL Error", MB_OK | MB_ICONERROR);
        DestroyWindow(hWnd);
        return 1;
    }

    // Check actual OpenGL version — need at least 2.0 for ImGui shaders
    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LOG_INFO("OpenGL version: %s", gl_version ? gl_version : "unknown");
    LOG_INFO("OpenGL renderer: %s", gl_renderer ? gl_renderer : "unknown");

    // Pick the best GLSL version the driver actually supports
    const char* glsl_version = "#version 130";  // default: OpenGL 3.0+
    if (gl_version) {
        int major = 0, minor = 0;
        sscanf(gl_version, "%d.%d", &major, &minor);
        if (major < 2) {
            wchar_t msg[512];
            swprintf(msg, 512, L"Your GPU reports OpenGL %hs\n"
                     L"(renderer: %hs)\n\n"
                     L"CANmatik requires at least OpenGL 2.0.\n"
                     L"Please update your graphics driver.",
                     gl_version, gl_renderer ? gl_renderer : "unknown");
            MessageBoxW(hWnd, msg, L"CANmatik — Unsupported GPU", MB_OK | MB_ICONERROR);
            CleanupGL(hWnd);
            DestroyWindow(hWnd);
            return 1;
        }
        if (major == 2) {
            glsl_version = "#version 110";  // OpenGL 2.x
        }
        // OpenGL 3.0+ keeps "#version 130"
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Apply color scheme (settings already loaded above)
    app.apply_color_scheme();

    // Resize window to saved dimensions
    {
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int cur_w = rc.right - rc.left;
        int cur_h = rc.bottom - rc.top;
        if (cur_w != app.window_width() || cur_h != app.window_height()) {
            SetWindowPos(hWnd, nullptr, 0, 0,
                         app.window_width(), app.window_height(),
                         SWP_NOMOVE | SWP_NOZORDER);
        }
    }

    // -----------------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------------
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render entire app
        app.render();

        // Render
        ImGui::Render();
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_hDC);
    }

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    app.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupGL(hWnd);
    DestroyWindow(hWnd);
    UnregisterClassW(CLASS_NAME, hInstance);

    return 0;
  } catch (const std::exception& ex) {
    std::string msg = "CANmatik encountered a fatal error:\n\n";
    msg += ex.what();
    MessageBoxA(nullptr, msg.c_str(), "CANmatik — Fatal Error", MB_OK | MB_ICONERROR);
    return 1;
  } catch (...) {
    MessageBoxA(nullptr, "CANmatik encountered an unknown fatal error.",
                "CANmatik — Fatal Error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
