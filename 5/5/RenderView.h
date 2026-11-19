#ifndef RENDER_VIEW_HPP
#define RENDER_VIEW_HPP

#include <minwindef.h>

enum class ViewMode : UINT
{
	Lit,
	Unlit,                 // albedo only, no lighting
	Wireframe,             // wireframe geometry, unlit, terrain colored by LOD
	LightingOnly,          // lighting on a grey material
	BufferVisualization,   // a G-buffer channel on top of the frame (drawn by the editor)
	Count
};

enum class DebugBuffer : UINT
{
	Overview,
	BaseColor,
	Normal,
	Metallic,
	Roughness,
	AO,
	Depth,
	Velocity,
	Shadow,
	Cascades,
	Count
};

inline const char* ToString(ViewMode m)
{
	static const char* names[] = { "Lit", "Unlit", "Wireframe", "Lighting Only", "Buffer Visualization" };
	static_assert(_countof(names) == (size_t)ViewMode::Count, "add a name for every ViewMode");
	return names[(UINT)m];
}

inline const char* ToString(DebugBuffer b)
{
	static const char* names[] = { "Overview", "Base Color", "Normal", "Metallic", "Roughness", "AO", "Depth", "Velocity", "Shadow", "Cascades" };
	static_assert(_countof(names) == (size_t)DebugBuffer::Count, "add a name for every DebugBuffer");
	return names[(UINT)b];
}

struct ViewFlags
{
	ViewMode Mode = ViewMode::Lit;
	DebugBuffer Buffer = DebugBuffer::Overview;
};

#endif // !RENDER_VIEW_HPP
