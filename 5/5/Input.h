#ifndef INPUT_HPP
#define INPUT_HPP

#include <windows.h>
#include <DirectXMath.h>

class Input
{
public:
    void OnMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void EndFrame();

    bool IsDown(int vk) const { return _down[vk & 0xFF]; }
    bool WasPressed(int vk) const { return _down[vk & 0xFF] && !_prev[vk & 0xFF]; }
    bool WasReleased(int vk) const { return !_down[vk & 0xFF] && _prev[vk & 0xFF]; }

    POINT MousePos() const { return _mouse; }
    DirectX::XMFLOAT2 MouseDelta() const { return { float(_mouse.x - _prevMouse.x), float(_mouse.y - _prevMouse.y) }; }
    float Wheel() const { return _wheel; }

    bool MouseCaptured = false;
    bool KeyboardCaptured = false;

private:
    bool _down[256] = {};
    bool _prev[256] = {};
    POINT _mouse = {};
    POINT _prevMouse = {};
    float _wheel = 0.0f;
};

#endif // !INPUT_HPP