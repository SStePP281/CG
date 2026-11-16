#include "Camera.h"

using namespace DirectX;

XMVECTOR Camera::Forward() const
{
    return XMVector3Normalize(XMVectorSet(cosf(Pitch) * sinf(Yaw), sinf(Pitch), cosf(Pitch) * cosf(Yaw), 0.0f));
}

XMVECTOR Camera::Right() const
{
    return XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), Forward()));
}

XMVECTOR Camera::Up() const
{
    return XMVector3Cross(Forward(), Right());
}

XMMATRIX Camera::View() const
{
    return XMMatrixLookToLH(XMLoadFloat3(&Position), Forward(), Up());
}

XMMATRIX Camera::Proj(float aspect) const
{
    return XMMatrixPerspectiveFovLH(FovY, aspect, NearZ, FarZ);
}

BoundingFrustum Camera::Frustum(float aspect) const
{
    BoundingFrustum f;
    BoundingFrustum::CreateFromMatrix(f, Proj(aspect));

    XMMATRIX invView = XMMatrixInverse(nullptr, View());
    f.Transform(f, invView);
    return f;
}