#ifndef NO_AA_HPP
#define NO_AA_HPP

#include "AntiAliasing.h"

class NoAA : public AntiAliasing
{
public:
	AAMode GetMode() const override { return AAMode::None; }

	UINT SrvSlotCount() const override { return 0; }

	void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const RenderSettings& res) override {}
	void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) override { _out = in.color.srvSlot; }
	UINT OutputSrvSlot() const override { return _out; }

private:
	UINT _out = 0;
};

#endif // !NO_AA_HPP
