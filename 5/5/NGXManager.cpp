#include "NGXManager.h"
#include <iostream>

NGXManager::NGXManager(ID3D12Device* device) : _device(device)
{
	_bInitialized = false;

    if (_device == nullptr)
    {
        std::cerr << "[NGX] D3D12 device is null!\n";
        return;
    }

    std::cout << "[NGX] Initializing...\n";

    NVSDK_NGX_Result initResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
                     ProjectID, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                     "1.0", ApplicationDataPath,
                     _device, nullptr, NVSDK_NGX_Version_API );

    if (NVSDK_NGX_FAILED(initResult))
    {
        std::cerr << "[NGX] Initialization failed. Error: " << static_cast<int>(initResult) << '\n';

        _device = nullptr;
        return;
    }

    _bInitialized = true;

    std::cout << "[NGX] Initialized successfully.\n";

    NVSDK_NGX_Result paramResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&_parameters);

    if (NVSDK_NGX_FAILED(paramResult) || _parameters == nullptr)
    {
        std::cerr << "[NGX] Failed to get capability parameters. Error: " << static_cast<int>(paramResult) << '\n';

        return;
    }

    std::cout << "[NGX] Capability parameters acquired.\n";
}

NGXManager::~NGXManager()
{
    if (!_bInitialized) { return; }

    if (_parameters != nullptr)
    {
        NVSDK_NGX_D3D12_DestroyParameters(_parameters);
        _parameters = nullptr;
    }

    const NVSDK_NGX_Result result =
        NVSDK_NGX_D3D12_Shutdown1(_device);

    if (NVSDK_NGX_FAILED(result))
    {
        std::cerr << "[NGX] Shutdown failed. Error: " << static_cast<int>(result) << '\n';
    }
    else
    {
        std::cout << "[NGX] Shutdown completed.\n";
    }

    _bInitialized = false;
    _device = nullptr;
}
