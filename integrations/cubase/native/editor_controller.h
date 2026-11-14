#pragma once

#include "editor_view.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace TuneLab::Cubase
{
// This address belongs to the plug-in module, independent of the host executable.
inline std::filesystem::path managedBundleDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&managedBundleDirectory), &module))
    {
        throw std::runtime_error("Cannot locate the plug-in module");
    }
    std::vector<wchar_t> filename(32768);
    const DWORD length =
        GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
    if (length == 0 || length >= filename.size())
    {
        throw std::runtime_error("Cannot read the plug-in module path");
    }
    return std::filesystem::path(filename.data()).parent_path().parent_path() / L"Resources" /
           L"managed";
}

class EditorController final : public Steinberg::Vst::EditController
{
  public:
    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) override
    {
        if (!name || std::strcmp(name, Steinberg::Vst::ViewType::kEditor) != 0)
        {
            return nullptr;
        }
        try
        {
            if (!document)
            {
                document = std::make_shared<EditorDocument>();
                document->managedDirectory = managedBundleDirectory();
            }
            return new EditorView(document);
        }
        catch (const std::exception &exception)
        {
            OutputDebugStringA(exception.what());
            return nullptr;
        }
    }

  private:
    std::shared_ptr<EditorDocument> document;
};
} // namespace TuneLab::Cubase
