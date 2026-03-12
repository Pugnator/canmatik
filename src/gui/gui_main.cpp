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
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int nCmdShow) {
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
        logger.configure(TraceType::file, "canmatik_gui.log", rotation);
        LOG_INFO("CANmatik GUI starting");
    }

    // GuiApp manages all state
    canmatik::GuiApp app;

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
        DestroyWindow(hWnd);
        return 1;
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
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initialize app (loads settings + applies color scheme)
    app.init("canmatik_gui.json");
    app.apply_color_scheme();

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
}
