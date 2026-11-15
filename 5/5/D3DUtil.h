#ifndef D3DUTIL_HPP
#define D3DUTIL_HPP

#include <string>
#include <unordered_map>

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>
#include <DirectXColors.h>
#include <dxgiformat.h>
#include <DirectXCollision.h>

#include "MathHelper.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class D3DUtil
{
public:
    static ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
    static UINT CalcConstantBufferSize(UINT byteSize);
    static ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& target);
};

#endif // !D3DUTIL_HPP