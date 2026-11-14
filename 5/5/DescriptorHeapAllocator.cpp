#include "DescriptorHeapAllocator.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include "ThrowIfFaild.h"

void DescriptorAllocator::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible)
{
	assert(!shaderVisible || type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = type;
	desc.NumDescriptors = capacity;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(_heap.GetAddressOf())));

	_cpuStart = _heap->GetCPUDescriptorHandleForHeapStart();
	_gpuStart = shaderVisible ? _heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };

	_descSize = device->GetDescriptorHandleIncrementSize(type);
	_capacity = capacity;
	_used = 0;

	_free.clear();
	_free.push_back({ 0, capacity });
}

DescriptorRange DescriptorAllocator::Allocate(UINT count)
{
	assert(count > 0);

    for (auto it = _free.begin(); it != _free.end(); ++it)
    {
        if (it->second < count) { continue; }

        DescriptorRange r = {};
        r._start = it->first;
        r._count = count;
        r._descSize = _descSize;

        r._cpuBase = { _cpuStart.ptr + SIZE_T(r._start) * _descSize };
        r._gpuBase = _gpuStart.ptr ? D3D12_GPU_DESCRIPTOR_HANDLE{ _gpuStart.ptr + UINT64(r._start) * _descSize } : D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };

        it->first += count;
        it->second -= count;

        if (it->second == 0) { _free.erase(it); }

        _used += count;

        return r;
    }

    throw std::runtime_error("Descriptor heap full: need " + std::to_string(count) + ", used " + std::to_string(_used) + "/" + std::to_string(_capacity));
}

void DescriptorAllocator::Free(DescriptorRange& range)
{
    if (!range.Valid()) { return; }

    assert(range._cpuBase.ptr == _cpuStart.ptr + SIZE_T(range._start) * _descSize);
    assert(range._start + range._count <= _capacity);

    const UINT start = range._start;
    const UINT count = range._count;

    auto next = std::upper_bound(_free.begin(), _free.end(), start, [](UINT s, const std::pair<UINT, UINT>& b) { return s < b.first; });

    assert(next == _free.end() || start + count <= next->first);
    assert(next == _free.begin() || std::prev(next)->first + std::prev(next)->second <= start);

    auto it = _free.insert(next, { start, count });

    auto after = std::next(it);
    if (after != _free.end() && it->first + it->second == after->first)
    {
        it->second += after->second;
        _free.erase(after);
    }

    if (it != _free.begin())
    {
        auto before = std::prev(it);
        if (before->first + before->second == it->first)
        {
            before->second += it->second;
            _free.erase(it);
        }
    }

    _used -= count;

    range = DescriptorRange{};
}
