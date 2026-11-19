#include "Window.h"
#include <windowsx.h>

static const wchar_t* WINDOW_CLASS = L"EngineWindow";

Window::Window(HINSTANCE hInstance, const std::wstring& title, DisplaySettings& settings) : _hInstance(hInstance), _size(settings.WindowSize)
{
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassW(&wc);

    RECT r = { 0, 0, (LONG)_size.Width, (LONG)_size.Height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    _hwnd = CreateWindowW(WINDOW_CLASS, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, this);

    ShowWindow(_hwnd, SW_SHOW);
    UpdateWindow(_hwnd);

    if (settings.Fullscreen) { SetFullscreen(true); }
}

Window::~Window()
{
    if (_hwnd) { DestroyWindow(_hwnd); }
    UnregisterClassW(WINDOW_CLASS, _hInstance);
}

bool Window::PumpMessages()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) { return false; }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void Window::SetClientSize(Resolution size)
{
    if (_fullscreen) { return; }

    RECT r = { 0, 0, (LONG)size.Width, (LONG)size.Height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::SetFullscreen(bool fullscreen)
{
    if (_fullscreen == fullscreen) { return; }
    _fullscreen = fullscreen;

    if (fullscreen)
    {
        GetWindowRect(_hwnd, &_windowedRect);

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& m = mi.rcMonitor;

        SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(_hwnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        SetWindowLongPtr(_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        const RECT& w = _windowedRect;
        SetWindowPos(_hwnd, nullptr, w.left, w.top, w.right - w.left, w.bottom - w.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

void Window::SetTitle(const std::wstring& title)
{
    SetWindowTextW(_hwnd, title.c_str());
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    auto* self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) { return DefWindowProc(hwnd, msg, wParam, lParam); }

    self->_hwnd = hwnd;
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (MessageHook && MessageHook(_hwnd, msg, wParam, lParam)) { return 1; }

    switch (msg)
    {
    case WM_SIZE:
    {
        _minimized = (wParam == SIZE_MINIMIZED);
        if (_minimized) { return 0; }

        _size = { LOWORD(lParam), HIWORD(lParam) };
        if (!_resizing && OnResize) { OnResize(_size); }
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        _resizing = true;
        return 0;

    case WM_EXITSIZEMOVE:
        _resizing = false;
        if (OnResize) { OnResize(_size); }
        return 0;

    case WM_ACTIVATE:
        _active = LOWORD(wParam) != WA_INACTIVE;
        return 0;

    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        SetCapture(_hwnd);
        break;

    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        ReleaseCapture();
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    switch (msg)
    {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_KILLFOCUS:
        if (OnInputMessage) { OnInputMessage(msg, wParam, lParam); }
        return 0;
    }

    return DefWindowProc(_hwnd, msg, wParam, lParam);
}