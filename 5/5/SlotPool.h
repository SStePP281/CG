#ifndef SLOT_POOL_HPP
#define SLOT_POOL_HPP

#include <memory>
#include <vector>

#include "SlotHandle.h"

template<class T>
class SlotPool
{
public:
    SlotHandle<T> Add(std::unique_ptr<T> obj)
    {
        uint32_t index;

        if (!_free.empty())
        {
            index = _free.back();
            _free.pop_back();
        }
        else
        {
            index = (uint32_t)_slots.size();
            _slots.emplace_back();
        }

        Slot& s = _slots[index];
        s.Obj = std::move(obj);
        _count++;

        return { index, s.Generation };
    }

    T* Get(SlotHandle<T> h)
    {
        if (!h.Valid() || h.Index >= _slots.size()) { return nullptr; }
        Slot& s = _slots[h.Index];
        return (s.Obj && s.Generation == h.Generation) ? s.Obj.get() : nullptr;
    }

    const T* Get(SlotHandle<T> h) const
    {
        return const_cast<SlotPool*>(this)->Get(h);
    }

    std::unique_ptr<T> Remove(SlotHandle<T> h)
    {
        if (!Get(h)) { return nullptr; }

        Slot& s = _slots[h.Index];
        std::unique_ptr<T> obj = std::move(s.Obj);

        s.Generation++;
        _free.push_back(h.Index);
        _count--;

        return obj;
    }

    template<class F>
    void ForEach(F&& f)
    {
        for (uint32_t i = 0; i < (uint32_t)_slots.size(); i++)
        {
            if (_slots[i].Obj) { f(SlotHandle<T>{ i, _slots[i].Generation }, * _slots[i].Obj); }
        }
    }

    size_t Count() const { return _count; }

private:
    struct Slot
    {
        std::unique_ptr<T> Obj;
        uint32_t Generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _free;
    size_t _count = 0;
};

#endif // !SLOT_POOL_HPP