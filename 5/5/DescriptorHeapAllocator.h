#ifndef DESCRIPTOR_HEAP_ALLOCATOR_HPP
#define DESCRIPTOR_HEAP_ALLOCATOR_HPP

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <utility>
#include <cassert>

using Microsoft::WRL::ComPtr;

class DescriptorRange
{
public:
    UINT Index(UINT i = 0) const
    {
        assert(i < _count);

        return _start + i;
    }

    DescriptorRange Sub(UINT i, UINT n = 1) const
    {
        assert(i + n <= _count);

        DescriptorRange r = *this;
        r._start += i;
        r._count = n;
        r._cpuBase = Cpu(i);
        r._gpuBase = _gpuBase.ptr ? Gpu(i) : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };

        return r;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT i = 0) const
    {
        assert(i < _count);

        return { _cpuBase.ptr + SIZE_T(i) * _descSize };
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT i = 0) const
    {
        assert(i < _count);
        assert(_gpuBase.ptr != 0 && "heap is not shader-visible");

        return { _gpuBase.ptr + UINT64(i) * _descSize };
    }

    UINT Count() const { return _count; }
    bool Valid() const { return _count != 0; }

private:
    friend class DescriptorAllocator;

    D3D12_CPU_DESCRIPTOR_HANDLE _cpuBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuBase{};
    UINT _start = 0;
    UINT _count = 0;
    UINT _descSize = 0;
};

class DescriptorAllocator
{
public:
    void Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible);

    DescriptorRange Allocate(UINT count = 1);
    void Free(DescriptorRange& range);

    ID3D12DescriptorHeap* Heap() const { return _heap.Get(); }
    UINT IncSize()  const { return _descSize; }
    UINT Capacity() const { return _capacity; }
    UINT Used()     const { return _used; }

private:
    ComPtr<ID3D12DescriptorHeap> _heap;
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuStart{};
    UINT _descSize = 0;
    UINT _capacity = 0;
    UINT _used = 0;

    std::vector<std::pair<UINT, UINT>> _free;
};

#endif // !DESCRIPTOR_HEAP_ALLOCATOR_HPP