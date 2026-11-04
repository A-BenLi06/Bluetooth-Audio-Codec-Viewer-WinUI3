#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Native
{
enum class ConnectionState { Checking, Connected, Disconnected, Unavailable };
struct BluetoothStatus
{
    ConnectionState state{};
    std::vector<std::wstring> names;
    std::vector<uint64_t> addresses;
    uint64_t revision{};
    uint32_t error{};
};
// Generation invalidates displayed observations when the connection/output
// changes. An active capture separately verifies its physical target on return.
struct BluetoothStatusTracker
{
    uint64_t revision{}, generation{};
    ConnectionState state{ConnectionState::Checking};
    std::vector<uint64_t> addresses;
    std::wstring outputId;
    bool Accept(BluetoothStatus const &status, std::wstring const &output)
    {
        if (status.revision <= revision)
            return false;
        revision = status.revision;
        if (state != status.state || addresses != status.addresses || outputId != output)
            ++generation;
        state = status.state;
        addresses = status.addresses;
        outputId = output;
        return true;
    }
};
} // namespace Native
