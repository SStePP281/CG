#ifndef FRAME_STATS_HPP
#define FRAME_STATS_HPP

#include <windows.h>
#include <cmath>

struct FrameStats
{
    UINT DrawCalls = 0;
    UINT ShadowDraws = 0;
    UINT Triangles = 0;
    UINT TotalInstances = 0;
    UINT VisibleInstances = 0;
    UINT TerrainTiles = 0;

    UINT SrvUsed = 0;
    UINT SrvCapacity = 0;

    UINT RtvUsed = 0;
    UINT RtvCapacity = 0;

    void ResetPerFrame()
    {
        DrawCalls = ShadowDraws = Triangles = 0;
        TotalInstances = VisibleInstances = TerrainTiles = 0;
    }
};

class FrameTimeHistory
{
public:
    static constexpr int SIZE = 144;

    void Push(float ms)
    {
        _values[_offset] = ms;
        _offset = (_offset + 1) % SIZE;
        if (_count < SIZE) { _count++; }
    }

    float Avg() const
    {
        if (!_count) { return 0.0f; }
        float sum = 0.0f;
        for (int i = 0; i < _count; i++) { sum += _values[i]; }
        return sum / _count;
    }

    float Max() const
    {
        float m = 0.0f;
        for (int i = 0; i < _count; i++) { m = fmax(m, _values[i]); }
        return m;
    }

    const float* Data() const { return _values; }
    int Offset() const { return _offset; }

private:
    float _values[SIZE] = {};
    int _offset = 0;
    int _count = 0;
};

#endif // !FRAME_STATS_HPP