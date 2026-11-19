#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <windows.h>
#include <string>
#include <functional>
#include "RenderSettings.h"

class Window
{
public:
    Window(HINSTANCE hInstance, const std::wstring& title, DisplaySettings& settings);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool PumpMessages();

    HWND Handle() const { return _hwnd; }
    Resolution Size() const { return _size; }
    bool Minimized() const { return _minimized; }
    bool Active() const { return _active; }

    void SetClientSize(Resolution size);
    void SetFullscreen(bool fullscreen);
    bool Fullscreen() const { return _fullscreen; }

    void SetTitle(const std::wstring& title);

    std::function<bool(HWND, UINT, WPARAM, LPARAM)> MessageHook;
    std::function<void(UINT, WPARAM, LPARAM)> OnInputMessage;
    std::function<void(Resolution)> OnResize;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE _hInstance = nullptr;
    HWND _hwnd = nullptr;
    Resolution _size;

    bool _fullscreen = false;
    RECT _windowedRect = {};

    bool _minimized = false;
    bool _resizing = false;
    bool _active = true;
};

#endif // !WINDOW_HPP