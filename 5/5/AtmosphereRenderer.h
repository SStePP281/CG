#ifndef ATMOSPHERE_RENDERER_HPP
#define ATMOSPHERE_RENDERER_HPP

#include "AtmosphereData.h"
#include "LightObject.h"
#include "ShaderTypes.h"
#include "UploadBuffer.h"
#include "DescriptorHeapAllocator.h"
#include "GraphicsDevice.h"

class AtmosphereRenderer
{
private:
	static constexpr float SUN_FADE_START = -0.05f;
	static constexpr float SUN_FADE_END = 0.02f;

public:
	void SetData(const AtmosphereData* data) { _data = data; }
	const AtmosphereData* Data() const { return _data; }

	bool NeedsRebuild(const AtmosphereData* data) const { return _data != data; }

	void Update(UploadBuffer<AtmosphereConstant>& cb);

	const LightObject* Sun() const { return _hasSun ? &_sun : nullptr; }

private:
	static float AirMass(float cosZenith);
	static DirectX::XMFLOAT3 SunTransmittance(const AtmosphereData& d);

	LightObject _sun = {};
	bool _hasSun = false;

private:
	const AtmosphereData* _data = nullptr;
};

#endif