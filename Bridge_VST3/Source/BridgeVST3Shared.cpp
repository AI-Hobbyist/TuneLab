#include "BridgeVST3Shared.h"

#ifdef _WIN32

#include <cstring>

namespace BridgeVST3 {

BridgeSession::~BridgeSession()
{
    shutdown();
}

bool BridgeSession::init (const std::string& sessionId)
{
    shutdown();

    mSessionId = sessionId;
    const std::string name = std::string (TL_BRIDGE_SHM_PREFIX) + sessionId;

    // 创建（若已存在则打开）命名文件映射。
    mMapping = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, TL_BRIDGE_CONTROL_SIZE, name.c_str());
    if (mMapping == nullptr)
        return false;

    const bool alreadyExists = (GetLastError() == ERROR_ALREADY_EXISTS);

    mControl = static_cast<TLBridgeControl*> (MapViewOfFile (mMapping, FILE_MAP_ALL_ACCESS, 0, 0, TL_BRIDGE_CONTROL_SIZE));
    if (mControl == nullptr)
    {
        CloseHandle (mMapping);
        mMapping = nullptr;
        return false;
    }

    if (!alreadyExists)
    {
        // 新会话：清零并写协议头。
        ZeroMemory (mControl, TL_BRIDGE_CONTROL_SIZE);
        mControl->magic = TL_BRIDGE_MAGIC;
        mControl->version = TL_BRIDGE_VERSION;
        mControl->connected = 0;
        mControl->protocolError = kTLBridgeErrorNone;
    }

    mControl->pluginPid = static_cast<uint32_t> (::GetCurrentProcessId());
    strncpy_s (mControl->sessionName, TL_BRIDGE_SESSION_NAME_MAX, mSessionId.c_str(), _TRUNCATE);

    mPluginTick = mControl->pluginTick;
    return true;
}

void BridgeSession::shutdown()
{
    if (mControl != nullptr)
    {
        UnmapViewOfFile (mControl);
        mControl = nullptr;
    }

    if (mMapping != nullptr)
    {
        CloseHandle (mMapping);
        mMapping = nullptr;
    }

    mSessionId.clear();
}

bool BridgeSession::tick()
{
    if (mControl == nullptr)
        return false;

    ++mPluginTick;
    mControl->pluginTick = mPluginTick;
    return true;
}

} // namespace BridgeVST3

#else
#error "Bridge_VST3 M0 仅支持 Windows（命名文件映射）；POSIX shm 待后续里程碑。"
#endif
