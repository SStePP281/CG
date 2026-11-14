#include "DeferredReleaseQueue.h"
#include <assert.h>

void DeferredReleaseQueue::Push(UINT64 fenceValue, ComPtr<ID3D12Resource> res, DescriptorAllocator* alloc, DescriptorRange range)
{
    assert(_entries.empty() || _entries.back().Fence <= fenceValue);
    assert(!range.Valid() || alloc);

    _entries.push_back({ fenceValue, std::move(res), alloc, range });
}

void DeferredReleaseQueue::Process(UINT64 completedFence)
{
    while (!_entries.empty() && _entries.front().Fence <= completedFence)
    {
        Entry& e = _entries.front();

        if (e.Alloc) { e.Alloc->Free(e.Range); }

        _entries.pop_front();
    }
}
