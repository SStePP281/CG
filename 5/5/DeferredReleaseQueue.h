#ifndef DEFERRED_RELEASE_QUEUE_HPP
#define DEFERRED_RELEASE_QUEUE_HPP

#include <d3d12.h>
#include <wrl/client.h>
#include <deque>
#include "DescriptorHeapAllocator.h"

using Microsoft::WRL::ComPtr;

class DeferredReleaseQueue
{
public:
    void Push(UINT64 fenceValue, ComPtr<ID3D12Resource> res, DescriptorAllocator* alloc = nullptr, DescriptorRange range = {});

    void Process(UINT64 completedFence);

    size_t Size() const { return _entries.size(); }

private:
    struct Entry
    {
        UINT64 Fence = 0;
        ComPtr<ID3D12Resource> Resource;
        DescriptorAllocator* Alloc = nullptr;
        DescriptorRange Range;
    };

    std::deque<Entry> _entries;
};

#endif // !DEFERRED_RELEASE_QUEUE_HPP