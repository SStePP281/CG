#ifndef NGX_MANAGER_HPP
#define NGX_MANAGER_HPP

#include <d3d12.h>
#include <nvsdk_ngx.h>

constexpr char ProjectID[] = "d83475ca-9e01-4122-9363-f399c9a0b48a";
constexpr wchar_t ApplicationDataPath[] = L"./NGX";

class NGXManager
{
public:
    explicit NGXManager(ID3D12Device* device);
    ~NGXManager();

    NGXManager() = delete;
    NGXManager(const NGXManager&) = delete;
    NGXManager& operator=(const NGXManager&) = delete;

    bool IsInitialized() const { return _bInitialized; }

    NVSDK_NGX_Parameter* GetParameters() const { return _parameters; }

private:
    ID3D12Device* _device;

    bool _bInitialized = false;
    NVSDK_NGX_Parameter* _parameters = nullptr;
};

#endif // !NGX_MANAGER_HPP
