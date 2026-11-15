#ifndef SLOT_HANDLE_HPP
#define SLOT_HANDLE_HPP

#include <cstdint>
#include <functional>

template<class T>
struct SlotHandle
{
	uint32_t Index = UINT32_MAX;
	uint32_t Generation = 0;

	bool Valid() const { return Index != UINT32_MAX; }

	bool operator==(const SlotHandle& other) const
	{
		return Index == other.Index && Generation == other.Generation;
	}

	bool operator!=(const SlotHandle& other) const
	{
		return !(*this == other);
	}
};

template<class T>
struct std::hash<SlotHandle<T>>
{
	size_t operator()(const SlotHandle<T>& h) const
	{
		return (size_t(h.Generation) << 32) | h.Index;
	}
};

#endif // !SLOT_HANDLE_HPP
