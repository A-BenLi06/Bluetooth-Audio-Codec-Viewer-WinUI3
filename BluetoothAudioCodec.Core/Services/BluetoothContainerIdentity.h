#pragma once
#include <guiddef.h>
#include <cstdint>
#include <span>
namespace Native
{
struct BluetoothContainerIdentity
{
    GUID container{};
    uint64_t address{};
};
inline uint64_t BluetoothAddressForContainer(GUID const &container,
                                            std::span<BluetoothContainerIdentity const> devices)
{
    // The machine's shared container cannot identify a remote peripheral.
    constexpr GUID machine{0, 0, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    if (IsEqualGUID(container, GUID{}) || IsEqualGUID(container, machine))
        return 0;
    uint64_t address{};
    for (auto const &device : devices)
    {
        if (!device.address || !IsEqualGUID(container, device.container))
            continue;
        if (address && address != device.address)
            return 0; // Ambiguous identity must never select somebody else's headset.
        address = device.address;
    }
    return address;
}
} // namespace Native
