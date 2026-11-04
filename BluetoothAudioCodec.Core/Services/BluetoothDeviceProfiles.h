#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Native
{
struct AudioOutput;
// Transient Windows values. Addresses/container IDs must be tagged before persistence.
struct BluetoothProductField
{
    std::wstring value, source{L"none"};
};
struct BluetoothDeviceProfile
{
    uint64_t address{};
    std::wstring container;
    BluetoothProductField model, manufacturer;
    std::wstring connection{L"unknown"}, metadataStatus{L"unavailable"};
};
struct BluetoothDeviceProfiles
{
    std::vector<BluetoothDeviceProfile> devices;
    std::optional<uint32_t> connectedCount;
    uint32_t omitted{};
    bool partial{};
};
BluetoothDeviceProfiles CaptureBluetoothDeviceProfiles(AudioOutput const &output) noexcept;
} // namespace Native
