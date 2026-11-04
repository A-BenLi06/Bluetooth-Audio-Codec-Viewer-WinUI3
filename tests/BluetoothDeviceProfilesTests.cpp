#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>
#include <iostream>
#include <stdexcept>
#include "../BluetoothAudioCodec.Core/Services/BluetoothDeviceProfiles.h"
#include "../BluetoothAudioCodec.Core/Services/NativeServices.h"
#include "../BluetoothAudioCodec.Core/Services/BluetoothConnectionQuery.h"
namespace Native
{
std::optional<BluetoothStatus> Injected;
BluetoothStatus ReadBluetoothConnections()
{
    if (Injected) return *Injected;
    struct Api
    {
        auto FirstRadio(BLUETOOTH_FIND_RADIO_PARAMS *p, HANDLE *h) { return BluetoothFindFirstRadio(p, h); }
        auto NextRadio(HBLUETOOTH_RADIO_FIND f, HANDLE *h) { return BluetoothFindNextRadio(f, h); }
        void CloseRadioFind(HBLUETOOTH_RADIO_FIND f) { BluetoothFindRadioClose(f); }
        void CloseRadio(HANDLE h) { CloseHandle(h); }
        auto FirstDevice(BLUETOOTH_DEVICE_SEARCH_PARAMS *p, BLUETOOTH_DEVICE_INFO *d) { return BluetoothFindFirstDevice(p, d); }
        auto NextDevice(HBLUETOOTH_DEVICE_FIND f, BLUETOOTH_DEVICE_INFO *d) { return BluetoothFindNextDevice(f, d); }
        void CloseDeviceFind(HBLUETOOTH_DEVICE_FIND f) { BluetoothFindDeviceClose(f); }
        DWORD Error() { return GetLastError(); }
    } api;
    return Detail::QueryBluetoothConnections(api);
}
}
void Require(bool condition) { if (!condition) throw std::runtime_error("device profile check failed"); }
int main()
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Native::AudioOutput target;
        target.name = L"Private nickname that must never become a model";
        Native::BluetoothStatus status;
        status.state = Native::ConnectionState::Disconnected;
        Native::Injected = status;
        auto empty = Native::CaptureBluetoothDeviceProfiles(target);
        Require(empty.devices.empty() && empty.connectedCount == 0u && !empty.partial);
        status.state = Native::ConnectionState::Connected;
        for (uint64_t i = 1; i <= 20; ++i) status.addresses.push_back(0x020000000000ULL + i);
        target.address = status.addresses.back();
        Native::Injected = status;
        auto bounded = Native::CaptureBluetoothDeviceProfiles(target);
        Require(bounded.devices.size() == 8 && bounded.omitted == 12 && bounded.partial && bounded.connectedCount == 20u);
        Require(bounded.devices.front().address == target.address);
        for (auto const &device : bounded.devices)
            Require(device.connection == L"connected" && device.model.value != target.name);
        status.state = Native::ConnectionState::Unavailable; status.addresses.clear();
        Native::Injected = status;
        auto unknown = Native::CaptureBluetoothDeviceProfiles(target);
        Require(unknown.partial && !unknown.connectedCount && unknown.devices.size() == 1 && unknown.devices[0].connection == L"unknown");
        Require(unknown.devices[0].model.value != target.name);
        Native::Injected.reset();
        auto live = Native::CaptureBluetoothDeviceProfiles({});
        size_t models{}, manufacturers{};
        for (auto const &device : live.devices)
        {
            models += !device.model.value.empty(); manufacturers += !device.manufacturer.value.empty();
            Require(device.model.value.empty() || device.model.source == L"windows-device-container");
        }
        // Only counts/status leave this read-only local probe, never device identifiers or names.
        std::cout << "{\"passed\":true,\"collector_cases\":3,\"live_cached_read\":true,\"live_devices\":" << live.devices.size()
                  << ",\"live_models\":" << models << ",\"live_manufacturers\":" << manufacturers
                  << ",\"live_partial\":" << (live.partial ? "true" : "false") << "}\n";
        return 0;
    }
    catch (...) { std::cout << "{\"passed\":false}\n"; return 1; }
}
