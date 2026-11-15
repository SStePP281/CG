#include "DeferredReleaseQueue.h"
#include <cassert>

void DeferredReleaseQueue::Push(ComPtr<ID3D12Resource> res, DescriptorAllocator* alloc, DescriptorRange range)
{
    assert(!range.Valid() || alloc);

    _pending.push_back({ 0, std::move(res), alloc, range });
}

void DeferredReleaseQueue::Submit(UINT64 fenceValue)
{
    assert(_entries.empty() || _entries.back().Fence <= fenceValue);

    for (Entry& e : _pending)
    {
        e.Fence = fenceValue;
        _entries.push_back(std::move(e));
    }

    _pending.clear();
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
