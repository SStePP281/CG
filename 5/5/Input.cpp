#include "Input.h"
#include <windowsx.h>
#include <cstring>

void Input::OnMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: _down[wParam & 0xFF] = true;  break;
    case WM_KEYUP:   case WM_SYSKEYUP:   _down[wParam & 0xFF] = false; break;

    case WM_LBUTTONDOWN: _down[VK_LBUTTON] = true;  break;
    case WM_LBUTTONUP:   _down[VK_LBUTTON] = false; break;
    case WM_RBUTTONDOWN: _down[VK_RBUTTON] = true;  break;
    case WM_RBUTTONUP:   _down[VK_RBUTTON] = false; break;
    case WM_MBUTTONDOWN: _down[VK_MBUTTON] = true;  break;
    case WM_MBUTTONUP:   _down[VK_MBUTTON] = false; break;

    case WM_MOUSEMOVE:
        _mouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        break;

    case WM_MOUSEWHEEL:
        _wheel += GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
        break;

    case WM_KILLFOCUS:
        std::memset(_down, 0, sizeof(_down));
        break;
    }
}

void Input::EndFrame()
{
    std::memcpy(_prev, _down, sizeof(_down));
    _prevMouse = _mouse;
    _wheel = 0.0f;
}