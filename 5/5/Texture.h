#ifndef TEXTURE_HPP
#define TEXTURE_HPP

#include <string>

#include <wrl/client.h>

#include "SlotHandle.h"
#include "DescriptorHeapAllocator.h"

using Microsoft::WRL::ComPtr;

struct Texture;
using TextureHandle = SlotHandle<Texture>;

struct Texture
{
	std::string Name;
	std::wstring Filename;

	ComPtr<ID3D12Resource> Resource = nullptr;
	DescriptorRange SrvRange = {};
};

#endif // !TEXTURE_HPP