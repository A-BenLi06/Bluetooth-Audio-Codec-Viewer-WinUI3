#pragma once
#include "BluetoothConnectionState.h"
#include <windows.h>
#include <bluetoothapis.h>
#include <map>
#include <set>
namespace Native
{
namespace Detail
{
template <typename Action> struct OnExit
{
    Action action;
    ~OnExit() { action(); }
};
// Inject the API to test adapter removal/errors without changing local radios.
template <typename Api> BluetoothStatus QueryBluetoothConnections(Api &api)
{
    BluetoothStatus status;
    status.state = ConnectionState::Disconnected;
    BLUETOOTH_FIND_RADIO_PARAMS parameters{sizeof(parameters)};
    HANDLE radio{};
    auto radios = api.FirstRadio(&parameters, &radio);
    if (!radios)
    {
        auto error = api.Error();
        if (error != ERROR_NO_MORE_ITEMS)
        {
            status.state = ConnectionState::Unavailable;
            status.error = error;
        }
        return status;
    }
    OnExit closeRadios{[&] { api.CloseRadioFind(radios); }};
    std::map<uint64_t, std::wstring> connected;
    do
    {
        OnExit closeRadio{[&] { api.CloseRadio(radio); }};
        BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof(search)};
        search.hRadio = radio;
        search.fReturnConnected = TRUE;
        // fIssueInquiry stays FALSE: read local state, never scan.
        BLUETOOTH_DEVICE_INFO device{sizeof(device)};
        auto devices = api.FirstDevice(&search, &device);
        if (devices)
        {
            OnExit closeDevices{[&] { api.CloseDeviceFind(devices); }};
            do
            {
                if (device.fConnected && ((device.ulClassofDevice >> 8) & 0x1f) == 4)
                    connected[device.Address.ullLong] = device.szName;
                device.dwSize = sizeof(device);
            } while (api.NextDevice(devices, &device));
            if (auto error = api.Error(); error != ERROR_NO_MORE_ITEMS)
                status.error = error;
        }
        else if (auto error = api.Error(); error != ERROR_NO_MORE_ITEMS)
            status.error = error;
    } while (api.NextRadio(radios, &radio));
    if (auto error = api.Error(); error != ERROR_NO_MORE_ITEMS)
        status.error = error;
    if (status.error)
    {
        status.state = ConnectionState::Unavailable;
        return status; // Never expose names from a partial/failed snapshot.
    }
    std::set<std::wstring> names;
    for (auto const &[address, name] : connected)
    {
        status.addresses.push_back(address);
        if (!name.empty())
            names.insert(name);
    }
    status.names.assign(names.begin(), names.end());
    if (!connected.empty())
        status.state = ConnectionState::Connected;
    return status;
}
} // namespace Detail
BluetoothStatus ReadBluetoothConnections();
} // namespace Native
