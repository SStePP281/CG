#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <DirectXMath.h>
#include <DirectXCollision.h>

class Camera
{
public:
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    float Yaw = 0.0f;
    float Pitch = 0.0f;

    float FovY = 0.25f * DirectX::XM_PI;
    float NearZ = 1.0f;
    float FarZ = 1000.0f;

    DirectX::XMVECTOR Forward() const;
    DirectX::XMVECTOR Right() const;
    DirectX::XMVECTOR Up() const;

    DirectX::XMMATRIX View() const;
    DirectX::XMMATRIX Proj(float aspect) const;

    DirectX::BoundingFrustum Frustum(float aspect) const;
};

#endif // !CAMERA_HPP