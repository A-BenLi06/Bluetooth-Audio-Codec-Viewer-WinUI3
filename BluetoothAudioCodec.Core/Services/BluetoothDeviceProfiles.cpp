#include <windows.h>
#include <objbase.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <functional>
#include <memory>
#include <chrono>
#include "BluetoothDeviceProfiles.h"
#include "NativeServices.h"
#include "BluetoothConnectionQuery.h"
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <map>

using namespace winrt;
using namespace Windows::Devices::Enumeration;
namespace Native
{
namespace
{
std::wstring Container(DEVINST node)
{
    GUID value{};
    DEVPROPTYPE type{};
    ULONG size = sizeof(value);
    constexpr GUID machine{0, 0, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_ContainerId, &type,
        reinterpret_cast<BYTE *>(&value), &size, 0) != CR_SUCCESS || type != DEVPROP_TYPE_GUID ||
        size != sizeof(value) || IsEqualGUID(value, GUID_NULL) || IsEqualGUID(value, machine)) return {};
    wchar_t text[40]{};
    StringFromGUID2(value, text, 40);
    return Upper(text);
}
std::wstring Manufacturer(DEVINST node)
{
    DEVPROPTYPE type{};
    ULONG size{};
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_Manufacturer, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
        type != DEVPROP_TYPE_STRING || size > 32768 || size < sizeof(wchar_t)) return {};
    std::vector<wchar_t> text(size / sizeof(wchar_t) + 1);
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_Manufacturer, &type,
        reinterpret_cast<BYTE *>(text.data()), &size, 0) != CR_SUCCESS || type != DEVPROP_TYPE_STRING) return {};
    return text.data();
}
std::wstring Property(DeviceInformation const &device, wchar_t const *key)
{
    auto value = device.Properties().TryLookup(key);
    if (auto property = value.try_as<Windows::Foundation::IPropertyValue>();
        property && property.Type() == Windows::Foundation::PropertyType::String)
        return std::wstring(property.GetString());
    return {};
}
} // namespace
BluetoothDeviceProfiles CaptureBluetoothDeviceProfiles(AudioOutput const &output) noexcept
{
    BluetoothDeviceProfiles result;
    try
    {
        auto connections = ReadBluetoothConnections(); // Local cache only; no inquiry/pair/connect.
        bool known = connections.state == ConnectionState::Connected || connections.state == ConnectionState::Disconnected;
        if (known) result.connectedCount = static_cast<uint32_t>(connections.addresses.size());
        else result.partial = true;
        std::vector<uint64_t> addresses;
        if (output.address) addresses.push_back(output.address); // Always retain the detection target first.
        for (auto address : connections.addresses)
            if (address && std::find(addresses.begin(), addresses.end(), address) == addresses.end()) addresses.push_back(address);
        if (addresses.size() > 8)
        {
            result.omitted = static_cast<uint32_t>(addresses.size() - 8);
            addresses.resize(8); result.partial = true;
        }
        // Only exact remote devnode addresses identify a peripheral. Do not use
        // display names, service IDs, an OUI guess, or the PC driver's provider.
        std::map<uint64_t, std::vector<std::pair<std::wstring, std::wstring>>> remote;
        std::map<std::wstring, uint64_t> owners;
        constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
        ULONG count{};
        bool enumerated = addresses.empty();
        for (int retry = 0; !enumerated && retry < 3; ++retry)
        {
            if (CM_Get_Device_ID_List_SizeW(&count, L"BTHENUM", flags) != CR_SUCCESS || !count || count > 1024 * 1024) break;
            std::vector<wchar_t> ids(count);
            auto status = CM_Get_Device_ID_ListW(L"BTHENUM", ids.data(), count, flags);
            if (status == CR_BUFFER_SMALL) continue;
            if (status != CR_SUCCESS) break;
            enumerated = true;
            for (auto id = ids.data(); *id; id += wcslen(id) + 1)
            {
                auto address = BluetoothAddressFromInstance(id);
                if (!address) continue;
                DEVINST node{};
                if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
                auto container = Container(node);
                if (!container.empty())
                {
                    auto [it, added] = owners.emplace(container, address);
                    if (!added && it->second != address) it->second = 0; // Ambiguous/shared container.
                }
                if (std::find(addresses.begin(), addresses.end(), address) != addresses.end())
                    remote[address].emplace_back(std::move(container), Manufacturer(node));
            }
        }
        if (!enumerated) result.partial = true;
        // Cached DeviceContainer reads have a total bounded wait; no live SDP/GATT queries.
        auto deadline = GetTickCount64() + 1200;
        for (auto address : addresses)
        {
            BluetoothDeviceProfile profile;
            profile.address = address;
            if (known && std::find(connections.addresses.begin(), connections.addresses.end(), address) != connections.addresses.end())
                profile.connection = L"connected";
            // Absence from the Classic Audio/Video set does not prove a LE target is disconnected.
            auto const &matches = remote[address];
            if (matches.size() > 1) profile.metadataStatus = L"ambiguous";
            else if (matches.size() == 1 && !matches[0].first.empty() && owners[matches[0].first] == address)
            {
                profile.container = matches[0].first;
                if (!matches[0].second.empty()) profile.manufacturer = {matches[0].second, L"windows-pnp"};
                try
                {
                    auto now = GetTickCount64();
                    if (now >= deadline) profile.metadataStatus = L"timeout";
                    else
                    {
                        auto operation = DeviceInformation::CreateFromIdAsync(profile.container,
                            {L"System.Devices.ModelName", L"System.Devices.Manufacturer"}, DeviceInformationKind::DeviceContainer);
                        auto state = operation.wait_for(std::chrono::milliseconds(std::min<uint64_t>(deadline - now, 300)));
                        if (state == Windows::Foundation::AsyncStatus::Started)
                        { operation.Cancel(); profile.metadataStatus = L"timeout"; }
                        else if (auto device = operation.GetResults()) // Propagate an actual API error as failed, not timeout.
                        {
                            auto model = Property(device, L"System.Devices.ModelName");
                            auto manufacturer = Property(device, L"System.Devices.Manufacturer");
                            if (!model.empty()) profile.model = {std::move(model), L"windows-device-container"};
                            if (!manufacturer.empty()) profile.manufacturer = {std::move(manufacturer), L"windows-device-container"};
                            profile.metadataStatus = L"collected";
                        }
                    }
                }
                catch (...) { profile.metadataStatus = L"failed"; }
            }
            else if (matches.size() == 1 && !matches[0].first.empty()) profile.metadataStatus = L"ambiguous";
            if (profile.metadataStatus == L"timeout" || profile.metadataStatus == L"failed") result.partial = true;
            result.devices.push_back(std::move(profile));
        }
    }
    catch (...) { result.partial = true; }
    return result;
}
} // namespace Native
