#ifndef NO_AA_HPP
#define NO_AA_HPP

#include "AntiAliasing.h"

class NoAA : public AntiAliasing
{
public:
	AAMode GetMode() const override { return AAMode::None; }

	void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const RenderSettings& res) override {}
	void Resolve(ID3D12GraphicsCommandList*, const AAInputs& in) override { _out = in.color.srv; }
	DescriptorRange OutputSrv() const override { return _out; }

private:
	DescriptorRange _out;
};

#endif // !NO_AA_HPP
