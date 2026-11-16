#ifndef LIGHT_OBJECT_HPP
#define LIGHT_OBJECT_HPP

#include "DirectXMath.h"

enum class LightType { Directional = 0, Point = 1, Spot = 2 };

struct LightObject
{
    LightType Type = LightType::Point;
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
    float FalloffStart = 1.0f;
    float FalloffEnd = 10.0f;
    float SpotPower = 64.0f;
    bool Active = true;
};

#endif // !LIGHT_OBJECT_HPP
