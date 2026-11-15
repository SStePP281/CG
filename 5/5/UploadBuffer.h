#ifndef UPLOAD_BUFFER_HPP
#define UPLOAD_BUFFER_HPP

#include <iostream>
#include <cstddef>

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>

#include "ThrowIfFaild.h"
#include "D3DUtil.h"

using Microsoft::WRL::ComPtr;

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
        _isConstantBuffer(isConstantBuffer)
    {
        _elementByteSize = sizeof(T);

        if (isConstantBuffer)
            _elementByteSize = D3DUtil::CalcConstantBufferSize(sizeof(T));

        auto propertie = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(_elementByteSize * elementCount);
        ThrowIfFailed(device->CreateCommittedResource(
            &propertie,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&_uploadBuffer)));

        ThrowIfFailed(_uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&_mappedData)));
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (_uploadBuffer != nullptr)
            _uploadBuffer->Unmap(0, nullptr);

        _mappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return _uploadBuffer.Get();
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&_mappedData[elementIndex * _elementByteSize], &data, sizeof(T));
    }

private:
    ComPtr<ID3D12Resource> _uploadBuffer;
    BYTE* _mappedData = nullptr;

    UINT _elementByteSize = 0;
    bool _isConstantBuffer = false;
};

#endif // !UPLOAD_BUFFER_HPP