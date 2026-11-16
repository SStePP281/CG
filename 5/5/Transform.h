#ifndef TRANSFORM_HPP
#define TRANSFORM_HPP

#include <DirectXMath.h>

struct Transform
{
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };

    DirectX::XMMATRIX Matrix() const
    {
        using namespace DirectX;
        return XMMatrixScalingFromVector(XMLoadFloat3(&Scale)) *
               XMMatrixRotationQuaternion(XMLoadFloat4(&Rotation)) *
               XMMatrixTranslationFromVector(XMLoadFloat3(&Position));
    }
};

#endif // !TRANSFORM_HPP