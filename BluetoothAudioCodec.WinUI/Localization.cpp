#include "pch.h"
#include "NativeServices.h"
using namespace winrt;
namespace Native {
hstring Localize(std::wstring_view key)
{
    static Microsoft::Windows::ApplicationModel::Resources::ResourceManager manager;
    try
    {
        return manager.MainResourceMap().GetSubtree(L"Resources").GetValue(hstring(key)).ValueAsString();
    }
    catch (...)
    {
        Log(L"localization", key);
        return hstring(key);
    }
}
}
